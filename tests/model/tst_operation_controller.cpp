/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/docker_endpoint.h"
#include "i18n.h"
#include "model/image_pull_model.h"
#include "model/operation_controller.h"
#include "backend/credential_store.h"
#include "support/fake_credential_backend.h"
#include "support/mock_docker_backend.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace Kontainer;
using Mutation = DockerBackendInterface::Mutation;
using MutationOutcome = DockerBackendInterface::MutationOutcome;

/*!
 * 写操作编排（ARCH_V4 §2.2.4 / §5.1）。
 *
 * 这里验证的是「上层规则」：准入、串行、结果通道、写后即读、权限降级。
 * 请求本身长什么样由 backend 的契约测试负责（tst_docker_backend_against_fake_engine）。
 */
class OperationControllerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();

    /* 权限门 */
    void writableEndpointAllowsOperations();
    void nonWritableEndpointHidesOperations();
    void enginePermissionDenialDegradesSessionToReadOnly();

    /* 串行与忙碌态 */
    void sameTargetIsSerialised();
    void differentTargetsRunInParallel();

    /* 结果通道 */
    void successRefreshesAffectedDataSets();
    void unchangedIsReportedAsSuccess();
    void failureCarriesCategoryAndEngineDetail();
    void removalEmitsNavigationSignal();
    void dismissClearsResult();

    /* 拉取 */
    void pullAppearsInTheListWithProgress();
    void differentImagesPullConcurrently();
    void pullDoesNotBlockOtherOperations();
    void cancelTargetsOnePull();
    void failedPullKeepsTheReason();
    void clearFinishedPullsKeepsActiveOnes();
    void pullImageUsesTheStoredCredential();
    void invalidReferenceIsRejectedBeforeBackend();

private:
    QString writableSocketPath();

    QTemporaryDir m_dir;
    MockDockerBackend *m_backend = nullptr;
    OperationController *m_operations = nullptr;
};

void OperationControllerTest::initTestCase()
{
    setupTranslationDomain();
    qRegisterMetaType<Kontainer::DockerError>("Kontainer::DockerError");
    qRegisterMetaType<Kontainer::ImagePullProgress>("Kontainer::ImagePullProgress");
    qRegisterMetaType<Kontainer::DockerBackendInterface::Mutation>("Kontainer::DockerBackendInterface::Mutation");
    qRegisterMetaType<Kontainer::DockerBackendInterface::MutationOutcome>("Kontainer::DockerBackendInterface::MutationOutcome");
}

void OperationControllerTest::init()
{
    delete m_operations;
    delete m_backend;
    m_backend = new MockDockerBackend(this);
    m_backend->setEndpoint(DockerEndpoint::unixSocket(writableSocketPath()));
    m_operations = new OperationController(m_backend, this);
}

QString OperationControllerTest::writableSocketPath()
{
    if (!m_dir.isValid()) {
        return {};
    }
    const QString path = m_dir.path() + QStringLiteral("/docker.sock");
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write("x");
        file.close();
    }
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
    return path;
}

void OperationControllerTest::writableEndpointAllowsOperations()
{
    QVERIFY(m_operations->writeAllowed());
    QCOMPARE(m_operations->writeAccessKey(), QStringLiteral("allowed"));
    QVERIFY(m_operations->writeAccessText().isEmpty());

    m_operations->startContainer(QStringLiteral("abc"));
    QCOMPARE(m_backend->mutationCount(Mutation::StartContainer), 1);
    QVERIFY(m_operations->busy());
}

void OperationControllerTest::nonWritableEndpointHidesOperations()
{
    m_backend->setEndpoint(DockerEndpoint::unixSocket(m_dir.path() + QStringLiteral("/missing.sock")));
    m_operations->refreshWriteAccess();

    QVERIFY(!m_operations->writeAllowed());
    QCOMPARE(m_operations->writeAccessKey(), QStringLiteral("denied"));
    QVERIFY2(!m_operations->writeAccessText().isEmpty(), "read-only mode must explain itself");

    // 界面本应隐藏写入口；即使被调用，也必须拒绝且不产生 backend 请求
    m_operations->startContainer(QStringLiteral("abc"));
    QCOMPARE(m_backend->mutationCount(Mutation::StartContainer), 0);
    QCOMPARE(m_operations->resultKey(), QStringLiteral("error"));
}

void OperationControllerTest::enginePermissionDenialDegradesSessionToReadOnly()
{
    QVERIFY(m_operations->writeAllowed());

    m_operations->startContainer(QStringLiteral("abc"));
    m_backend->completeMutations(MutationOutcome::Failed, DockerError(DockerError::Kind::PermissionDenied, QStringLiteral("access denied"), 403));

    // 降级是本次会话的最终状态：不再出现写入口，也不可恢复
    QVERIFY(!m_operations->writeAllowed());
    QCOMPARE(m_operations->resultKey(), QStringLiteral("error"));
    QCOMPARE(m_operations->resultCategoryKey(), QStringLiteral("userActionable"));
    QVERIFY(!m_operations->writeAccessText().isEmpty());

    // refreshWriteAccess() 不能把降级撤销（socket 明明是可写的）
    m_operations->refreshWriteAccess();
    QVERIFY(!m_operations->writeAllowed());

    m_operations->stopContainer(QStringLiteral("abc"));
    QCOMPARE(m_backend->mutationCount(Mutation::StopContainer), 0);
}

void OperationControllerTest::sameTargetIsSerialised()
{
    QSignalSpy stateSpy(m_operations, &OperationController::stateChanged);

    m_operations->startContainer(QStringLiteral("abc"));
    QCOMPARE(m_operations->activeCount(), 1);
    QVERIFY(m_operations->isTargetBusy(QStringLiteral("container:abc")));

    // 同一目标再来一次：拒绝，且不产生第二个请求
    m_operations->restartContainer(QStringLiteral("abc"));
    QCOMPARE(m_backend->mutationCount(Mutation::RestartContainer), 0);
    QCOMPARE(m_operations->resultCategoryKey(), QStringLiteral("userActionable"));
    QVERIFY(stateSpy.count() >= 1);

    m_backend->completeMutations();
    QVERIFY(!m_operations->busy());
    QVERIFY(!m_operations->isTargetBusy(QStringLiteral("container:abc")));
}

void OperationControllerTest::differentTargetsRunInParallel()
{
    m_operations->startContainer(QStringLiteral("abc"));
    m_operations->startContainer(QStringLiteral("def"));
    QCOMPARE(m_operations->activeCount(), 2);
    QCOMPARE(m_backend->mutationCount(Mutation::StartContainer), 2);
    QVERIFY(m_operations->isTargetBusy(QStringLiteral("container:abc")));
    QVERIFY(m_operations->isTargetBusy(QStringLiteral("container:def")));

    m_backend->completeMutations();
    QVERIFY(!m_operations->busy());
}

void OperationControllerTest::successRefreshesAffectedDataSets()
{
    const int containersBefore = m_backend->refreshCount(DockerBackendInterface::Section::Containers);
    const int storageBefore = m_backend->refreshCount(DockerBackendInterface::Section::Storage);
    const int imagesBefore = m_backend->refreshCount(DockerBackendInterface::Section::Images);

    m_operations->startContainer(QStringLiteral("abc"));
    m_backend->completeMutations();

    QCOMPARE(m_operations->resultKey(), QStringLiteral("success"));
    QVERIFY(!m_operations->resultText().isEmpty());
    // 写后即读：容器列表与存储占用必须立刻刷新
    QCOMPARE(m_backend->refreshCount(DockerBackendInterface::Section::Containers), containersBefore + 1);
    QCOMPARE(m_backend->refreshCount(DockerBackendInterface::Section::Storage), storageBefore + 1);
    QCOMPARE(m_backend->refreshCount(DockerBackendInterface::Section::Images), imagesBefore);
}

void OperationControllerTest::unchangedIsReportedAsSuccess()
{
    m_operations->stopContainer(QStringLiteral("abc"));
    m_backend->completeMutations(MutationOutcome::Unchanged);

    QCOMPARE(m_operations->resultKey(), QStringLiteral("unchanged"));
    QVERIFY(!m_operations->resultText().isEmpty());
    QCOMPARE(m_operations->resultCategoryKey(), QStringLiteral("none"));
}

void OperationControllerTest::failureCarriesCategoryAndEngineDetail()
{
    m_operations->removeContainer(QStringLiteral("abc"));
    m_backend->completeMutations(MutationOutcome::Failed,
                                 DockerError(DockerError::Kind::Conflict,
                                             QStringLiteral("You cannot remove a running container abc. Stop the container before attempting removal"),
                                             409));

    QCOMPARE(m_operations->resultKey(), QStringLiteral("error"));
    QCOMPARE(m_operations->resultCategoryKey(), QStringLiteral("userActionable"));
    // 引擎原文必须能拿到：文案是摘要，detail 才是「为什么」
    QVERIFY(m_operations->resultDetailText().contains(QStringLiteral("running container")));
    // 409 不是权限问题：不能因为一次冲突就降级为只读
    QVERIFY(m_operations->writeAllowed());
}

void OperationControllerTest::removalEmitsNavigationSignal()
{
    QSignalSpy removedSpy(m_operations, &OperationController::containerRemoved);
    QSignalSpy stateSpy(m_operations, &OperationController::containerStateChanged);

    m_operations->removeContainer(QStringLiteral("abc"));
    m_backend->completeMutations();
    QCOMPARE(removedSpy.count(), 1);
    QCOMPARE(removedSpy.at(0).at(0).toString(), QStringLiteral("abc"));
    QCOMPARE(stateSpy.count(), 0);

    m_operations->startContainer(QStringLiteral("def"));
    m_backend->completeMutations();
    QCOMPARE(stateSpy.count(), 1);
    QCOMPARE(stateSpy.at(0).at(0).toString(), QStringLiteral("def"));
}

void OperationControllerTest::dismissClearsResult()
{
    m_operations->startContainer(QStringLiteral("abc"));
    m_backend->completeMutations();
    QVERIFY(!m_operations->resultText().isEmpty());

    m_operations->dismissResult();
    QCOMPARE(m_operations->resultKey(), QStringLiteral("none"));
    QVERIFY(m_operations->resultText().isEmpty());
}

/*!
 * 拉取列表（ARCH_V4 §2.4 的落地）：拉取是长任务，必须能在后台继续、
 * 可以同时拉多个不同镜像、失败原因留在列表里而不是被静默丢掉。
 */
void OperationControllerTest::pullAppearsInTheListWithProgress()
{
    QSignalSpy listSpy(m_operations, &OperationController::pullListChanged);
    m_operations->pullImage(QStringLiteral("alpine"));

    QCOMPARE(m_backend->mutationCount(Mutation::PullImage), 1);
    QCOMPARE(m_backend->lastMutationTarget(Mutation::PullImage), QStringLiteral("image:alpine:latest"));
    QCOMPARE(m_operations->pulls()->count(), 1);
    QCOMPARE(m_operations->activePullCount(), 1);
    QVERIFY(m_operations->pulling());
    // 归一化后的引用（缺 tag → latest）才是用户看到的目标
    QCOMPARE(m_operations->pulls()->index(0, 0).data(ImagePullModel::ReferenceRole).toString(), QStringLiteral("alpine:latest"));
    QVERIFY(!m_operations->pulls()->index(0, 0).data(ImagePullModel::ProgressKnownRole).toBool());

    ImagePullProgress progress;
    progress.reference = QStringLiteral("alpine:latest");
    progress.phase = ImagePullProgress::Phase::Downloading;
    progress.statusText = QStringLiteral("Downloading");
    progress.currentBytes = 30;
    progress.totalBytes = 100;
    progress.totalLayers = 2;
    progress.completedLayers = 1;
    m_backend->emitPullProgress(progress);

    const QModelIndex index = m_operations->pulls()->index(0, 0);
    QVERIFY(index.data(ImagePullModel::ProgressKnownRole).toBool());
    QCOMPARE(index.data(ImagePullModel::ProgressRole).toDouble(), 0.3);
    QCOMPARE(index.data(ImagePullModel::StatusTextRole).toString(), QStringLiteral("Downloading"));
    QCOMPARE(index.data(ImagePullModel::TotalLayersRole).toInt(), 2);

    m_backend->completeMutations();
    QVERIFY(!m_operations->pulling());
    QCOMPARE(m_operations->activePullCount(), 0);
    // 成功后记录留在列表里（带「已完成」状态），而不是凭空消失
    QCOMPARE(m_operations->pulls()->count(), 1);
    QCOMPARE(m_operations->pulls()->index(0, 0).data(ImagePullModel::StatusKeyRole).toString(), QStringLiteral("succeeded"));
    QCOMPARE(m_operations->resultKey(), QStringLiteral("success"));
    QVERIFY(!listSpy.isEmpty());
}

void OperationControllerTest::differentImagesPullConcurrently()
{
    m_operations->pullImage(QStringLiteral("alpine"));
    m_operations->pullImage(QStringLiteral("busybox:latest"));

    // 两路并发：互不影响（这是这次改动的核心诉求）
    QCOMPARE(m_backend->mutationCount(Mutation::PullImage), 2);
    QCOMPARE(m_operations->activePullCount(), 2);
    QCOMPARE(m_operations->pulls()->count(), 2);

    // 同一个引用重复拉取：拒绝，并且不产生第二个请求
    m_operations->pullImage(QStringLiteral("alpine"));
    QCOMPARE(m_backend->mutationCount(Mutation::PullImage), 2);
    QCOMPARE(m_operations->resultKey(), QStringLiteral("error"));

    m_backend->completeMutations();
    QCOMPARE(m_operations->activePullCount(), 0);
}

void OperationControllerTest::pullDoesNotBlockOtherOperations()
{
    m_operations->pullImage(QStringLiteral("alpine"));
    QVERIFY(m_operations->pulling());
    // 拉取可能跑很久，不该让整页进入「忙」状态（否则刷新按钮会一直闪）
    QVERIFY(!m_operations->busy());

    m_operations->startContainer(QStringLiteral("cid-1"));
    QCOMPARE(m_backend->mutationCount(Mutation::StartContainer), 1);
}

/*!
 * 私有仓库：拉取时把钱包里对应仓库的凭据交给后端（ARCH_V5_V8 §2.6）。
 *
 * 这里断言的是"凭据真的到了拉取路径"，且没有凭据时保持匿名——
 * 拉取失败（401）由引擎给出，界面照旧显示原因，不会因为钱包不可用而整条路断掉。
 */
void OperationControllerTest::pullImageUsesTheStoredCredential()
{
    FakeCredentialBackend wallet;
    CredentialStore store(&wallet);
    store.open();
    m_operations->setCredentialStore(&store);

    RegistryCredential ghcr;
    ghcr.serverAddress = QStringLiteral("ghcr.io");
    ghcr.username = QStringLiteral("alice");
    ghcr.password = QStringLiteral("s3cret");
    QVERIFY(store.store(ghcr));

    m_operations->pullImage(QStringLiteral("ghcr.io/team/app:1.0"));
    QCOMPARE(m_backend->lastPullCredential().username, QStringLiteral("alice"));
    QCOMPARE(m_backend->lastPullCredential().password, QStringLiteral("s3cret"));
    QCOMPARE(m_backend->lastPullCredential().serverAddress, QStringLiteral("ghcr.io"));

    // 没有凭据的仓库：匿名拉取（凭据为空，而不是上一条的残留）
    m_operations->pullImage(QStringLiteral("alpine:3.19"));
    QVERIFY2(m_backend->lastPullCredential().isEmpty(), "an unauthenticated registry must pull anonymously");

    // 钱包不可用（未打开）：同样退化成匿名，而不是拒绝拉取
    CredentialStore closedWallet(&wallet);
    m_operations->setCredentialStore(&closedWallet);
    m_operations->pullImage(QStringLiteral("ghcr.io/team/other:2.0"));
    QVERIFY(m_backend->lastPullCredential().isEmpty());
    QCOMPARE(m_backend->lastPullCredential().serverAddress, QString());
}

void OperationControllerTest::cancelTargetsOnePull()
{
    m_operations->pullImage(QStringLiteral("alpine"));
    m_operations->pullImage(QStringLiteral("busybox"));
    m_operations->cancelPull(QStringLiteral("busybox:latest"));

    // 只取消点中的那一路
    QCOMPARE(m_backend->cancelledPulls(), QStringList {QStringLiteral("busybox:latest")});

    m_backend->completeMutations(MutationOutcome::Cancelled);
    QCOMPARE(m_operations->activePullCount(), 0);
    QCOMPARE(m_operations->resultKey(), QStringLiteral("cancelled"));
    // 取消不是错误：不产生错误样式，也不影响写权限
    QCOMPARE(m_operations->resultCategoryKey(), QStringLiteral("none"));
    QVERIFY(m_operations->writeAllowed());

    m_operations->cancelAllPulls();
    QCOMPARE(m_backend->cancelAllCount(), 1);
}

void OperationControllerTest::failedPullKeepsTheReason()
{
    m_operations->pullImage(QStringLiteral("nope/nope:none"));

    ImagePullProgress failed;
    failed.reference = QStringLiteral("nope/nope:none");
    failed.phase = ImagePullProgress::Phase::Failed;
    failed.errorText = QStringLiteral("manifest unknown");
    m_backend->emitPullProgress(failed);

    m_backend->completeMutations(MutationOutcome::Failed, DockerError(DockerError::Kind::EngineError, QStringLiteral("manifest unknown")));

    // 失败必须**留在列表里**并带原因：点开拉取窗口再关掉不会把失败吞掉
    QCOMPARE(m_operations->pulls()->count(), 1);
    const QModelIndex index = m_operations->pulls()->index(0, 0);
    QCOMPARE(index.data(ImagePullModel::StatusKeyRole).toString(), QStringLiteral("failed"));
    QCOMPARE(index.data(ImagePullModel::DetailTextRole).toString(), QStringLiteral("manifest unknown"));
    QCOMPARE(m_operations->resultKey(), QStringLiteral("error"));

    // 用户处理完可以移除；移除后列表为空
    m_operations->dismissPull(QStringLiteral("nope/nope:none"));
    QCOMPARE(m_operations->pulls()->count(), 0);
    QCOMPARE(m_operations->activePullCount(), 0);
}

void OperationControllerTest::clearFinishedPullsKeepsActiveOnes()
{
    m_operations->pullImage(QStringLiteral("alpine"));
    m_operations->pullImage(QStringLiteral("busybox"));
    // 第一路结束，第二路继续
    m_backend->completeMutations(MutationOutcome::Succeeded);

    QCOMPARE(m_operations->pulls()->count(), 2);
    QCOMPARE(m_operations->activePullCount(), 0);

    m_operations->clearFinishedPulls();
    QCOMPARE(m_operations->pulls()->count(), 0);
}

void OperationControllerTest::invalidReferenceIsRejectedBeforeBackend()
{
    m_operations->pullImage(QStringLiteral("alpine 3.19"));
    QCOMPARE(m_backend->mutationCount(Mutation::PullImage), 0);
    QCOMPARE(m_operations->resultKey(), QStringLiteral("error"));
    QVERIFY(!m_operations->pulling());
    QCOMPARE(m_operations->pulls()->count(), 0);

    QVERIFY(m_operations->isValidImageReference(QStringLiteral("alpine")));
    QVERIFY(!m_operations->isValidImageReference(QString()));
    QCOMPARE(m_operations->normalizedImageReference(QStringLiteral("alpine")), QStringLiteral("alpine:latest"));
}

QTEST_MAIN(OperationControllerTest)

#include "tst_operation_controller.moc"
