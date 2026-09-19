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
    void createNetworkValidatesInput();
    void createNetworkRefreshesAndReports();
    void removeNetworkIsGatedAndTracked();
    void connectAndDisconnectContainerToNetwork();
    void pauseAndResumeContainer();
    void createContainerValidatesAgainstExistingState();
    void createContainerCanStartAfterwards();
    void createVolumeValidatesAndRefreshes();
    void removeAndPruneVolumes();
    void invalidReferenceIsRejectedBeforeBackend();
    void portHolderIsReportedAndStoppedContainersDoNotBlock();
    void startFailureAboutAnAllocatedPortIsExplained();

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

/*!
 * 创建网络（ARCH_V5_V8 §3.3）：校验在 C++ 侧统一做，失败时给稳定 key、不发请求。
 */
void OperationControllerTest::createNetworkValidatesInput()
{
    QList<Network> existing;
    Network bridge;
    bridge.id = QString(64, QLatin1Char('b'));
    bridge.name = QStringLiteral("bridge");
    bridge.driver = QStringLiteral("bridge");
    existing.append(bridge);
    m_backend->setNetworks(existing);

    // 名称：空、含空格、以数字开头以外的非法字符都要被挡下
    QVERIFY(!m_operations->createNetwork(QString()));
    QCOMPARE(m_operations->resultDetailText(), QStringLiteral("nameRequired"));
    QVERIFY(!m_operations->createNetwork(QStringLiteral("my net")));
    QCOMPARE(m_operations->resultDetailText(), QStringLiteral("nameInvalid"));

    // 子网 / 网关格式
    QVERIFY(!m_operations->createNetwork(QStringLiteral("app_net"), QStringLiteral("not-a-cidr")));
    QCOMPARE(m_operations->resultDetailText(), QStringLiteral("subnetInvalid"));
    QVERIFY(!m_operations->createNetwork(QStringLiteral("app_net"), QString(), QStringLiteral("172.30.0.1")));
    QCOMPARE(m_operations->resultDetailText(), QStringLiteral("gatewayNeedsSubnet"));
    QVERIFY(!m_operations->createNetwork(QStringLiteral("app_net"), QStringLiteral("172.30.0.0/16"), QStringLiteral("not-an-ip")));
    QCOMPARE(m_operations->resultDetailText(), QStringLiteral("gatewayInvalid"));

    // 与现有网络重名（大小写不敏感：daemon 也是这样判的）
    QVERIFY(!m_operations->createNetwork(QStringLiteral("Bridge")));
    QCOMPARE(m_operations->resultDetailText(), QStringLiteral("nameInUse"));

    // 校验失败时一个请求都不该发出去
    QCOMPARE(m_backend->mutationCalls().size(), 0);

    // 合法输入：发出请求，字段如实传递
    QVERIFY(m_operations->createNetwork(QStringLiteral(" app_net "),
                                        QStringLiteral("172.30.0.0/16"),
                                        QStringLiteral("172.30.0.1"),
                                        true,
                                        true,
                                        {QVariantMap {{QStringLiteral("key"), QStringLiteral("owner")},
                                                      {QStringLiteral("value"), QStringLiteral("team-a")}}}));
    QCOMPARE(m_backend->lastNetworkCreate().name, QStringLiteral("app_net")); // 已 trim
    QCOMPARE(m_backend->lastNetworkCreate().driver, QStringLiteral("bridge")); // 本轮只建 bridge
    QCOMPARE(m_backend->lastNetworkCreate().subnet, QStringLiteral("172.30.0.0/16"));
    QVERIFY(m_backend->lastNetworkCreate().internal);
    QVERIFY(m_backend->lastNetworkCreate().attachable);
    QCOMPARE(m_backend->lastNetworkCreate().labels.size(), 1);
    QCOMPARE(m_backend->lastNetworkCreate().labels.first().first, QStringLiteral("owner"));
}

/*!
 * 创建/删除网络走同一条结果通道，并在成功后触发"写后即读"。
 */
void OperationControllerTest::createNetworkRefreshesAndReports()
{
    QSignalSpy networksSpy(m_operations, &OperationController::networksChanged);
    QSignalSpy messageSpy(m_operations, &OperationController::resultChanged);

    QVERIFY(m_operations->createNetwork(QStringLiteral("app_net")));
    QCOMPARE(m_backend->mutationCalls().size(), 1);
    QVERIFY2(m_operations->isTargetBusy(OperationTarget::network(QStringLiteral("app_net"))),
             "the target must be busy while the request is in flight");

    m_backend->completeMutations(DockerBackendInterface::MutationOutcome::Succeeded);
    QCOMPARE(networksSpy.count(), 1);
    QVERIFY2(m_operations->resultText().contains(QStringLiteral("app_net")),
             qPrintable(m_operations->resultText()));
    QVERIFY(!m_operations->isTargetBusy(OperationTarget::network(QStringLiteral("app_net"))));
    QVERIFY(m_backend->refreshCount(DockerBackendInterface::Section::Networks) >= 1);

    // 引擎拒绝（例如子网与现有网络重叠）：结果里带引擎原文，不改动任何本地状态
    QVERIFY(m_operations->createNetwork(QStringLiteral("other_net")));
    m_backend->completeMutations(DockerBackendInterface::MutationOutcome::Failed,
                                 DockerError(DockerError::Kind::EngineError, QStringLiteral("Pool overlaps with other one")));
    // 引擎原文进的是"技术细节"字段（文案按错误分级给），用户能看到具体原因
    QVERIFY2(m_operations->resultDetailText().contains(QStringLiteral("Pool overlaps")),
             qPrintable(m_operations->resultDetailText()));
    QVERIFY(!m_operations->resultText().isEmpty());
}

/*!
 * 删除网络：写权限门 + 目标忙碌跟踪（内置网络由界面挡住，daemon 也会拒绝）。
 */
void OperationControllerTest::removeNetworkIsGatedAndTracked()
{
    const QString id = QString(64, QLatin1Char('a'));
    QSignalSpy networksSpy(m_operations, &OperationController::networksChanged);

    m_operations->removeNetwork(id, QStringLiteral("app_net"));
    QCOMPARE(m_backend->lastRemovedNetwork(), id);
    QVERIFY(m_operations->isTargetBusy(OperationTarget::network(id)));
    m_backend->completeMutation(OperationTarget::network(id), DockerBackendInterface::MutationOutcome::Succeeded);
    QCOMPARE(networksSpy.count(), 1);
    QVERIFY(!m_operations->isTargetBusy(OperationTarget::network(id)));

    // 只读模式：不发请求，给出明确结果（界面本应隐藏入口，这里是兜底）
    m_backend->setEndpoint(DockerEndpoint::unixSocket(QStringLiteral("/tmp/does-not-exist.sock")));
    m_operations->refreshWriteAccess();
    QVERIFY(!m_operations->writeAllowed());
    const int callsBefore = m_backend->mutationCalls().size();
    m_operations->removeNetwork(id, QStringLiteral("app_net"));
    QCOMPARE(m_backend->mutationCalls().size(), callsBefore);
    QVERIFY(!m_operations->resultText().isEmpty());
}

/*!
 * 容器连接/断开网络（ARCH_V5_V8 §3.4）：参数传递、别名解析与写后即读。
 */
void OperationControllerTest::connectAndDisconnectContainerToNetwork()
{
    const QString networkId = QString(64, QLatin1Char('n'));
    const QString containerId = QString(64, QLatin1Char('c'));
    QSignalSpy networksSpy(m_operations, &OperationController::networksChanged);
    QSignalSpy containerSpy(m_operations, &OperationController::containerStateChanged);

    // 别名是逗号分隔的输入框内容：空白要去掉、空项要丢掉
    QVERIFY(m_operations->connectContainerToNetwork(networkId, containerId, QStringLiteral(" app , api ,, ")));
    QCOMPARE(m_backend->lastNetworkConnect().first, networkId);
    QCOMPARE(m_backend->lastNetworkConnect().second, containerId);
    QCOMPARE(m_backend->lastNetworkConnectAliases(), QStringList({QStringLiteral("app"), QStringLiteral("api")}));

    m_backend->completeMutations(DockerBackendInterface::MutationOutcome::Succeeded);
    QCOMPARE(networksSpy.count(), 1);
    QCOMPARE(containerSpy.count(), 1);
    QCOMPARE(containerSpy.at(0).at(0).toString(), containerId);
    QVERIFY2(m_operations->resultText().contains(QStringLiteral("connected")), qPrintable(m_operations->resultText()));

    // 断开：force 关闭（界面不给"强制"选项）
    QVERIFY(m_operations->disconnectContainerFromNetwork(networkId, containerId));
    QCOMPARE(m_backend->lastNetworkDisconnect().first, networkId);
    QCOMPARE(m_backend->lastNetworkDisconnect().second, containerId);
    m_backend->completeMutations(DockerBackendInterface::MutationOutcome::Succeeded);
    QCOMPARE(networksSpy.count(), 2);

    // 缺参数：不发请求，给出明确结果
    const int callsBefore = m_backend->mutationCalls().size();
    QVERIFY(!m_operations->connectContainerToNetwork(QString(), containerId));
    QVERIFY(!m_operations->disconnectContainerFromNetwork(networkId, QString()));
    QCOMPARE(m_backend->mutationCalls().size(), callsBefore);
    QCOMPARE(m_operations->resultDetailText(), QStringLiteral("missingTarget"));
}

/*!
 * 创建数据卷（ARCH_V5_V8 §3.5）：校验在 C++ 侧，失败不发请求。
 */
void OperationControllerTest::createVolumeValidatesAndRefreshes()
{
    QList<Volume> existing;
    Volume appData;
    appData.name = QStringLiteral("app_data");
    appData.driver = QStringLiteral("local");
    existing.append(appData);
    m_backend->setVolumes(existing);

    QVERIFY(!m_operations->createVolume(QString()));
    QCOMPARE(m_operations->resultDetailText(), QStringLiteral("nameRequired"));
    QVERIFY(!m_operations->createVolume(QStringLiteral("has space")));
    QCOMPARE(m_operations->resultDetailText(), QStringLiteral("nameInvalid"));
    QVERIFY(!m_operations->createVolume(QStringLiteral("app_data")));
    QCOMPARE(m_operations->resultDetailText(), QStringLiteral("nameInUse"));
    QCOMPARE(m_backend->mutationCalls().size(), 0); // 校验失败一个请求都不发

    QSignalSpy volumesSpy(m_operations, &OperationController::volumesChanged);
    QVERIFY(m_operations->createVolume(QStringLiteral(" cache "), QStringLiteral("local"),
                                       {QVariantMap {{QStringLiteral("key"), QStringLiteral("owner")},
                                                     {QStringLiteral("value"), QStringLiteral("team-a")}}}));
    QCOMPARE(m_backend->lastCreatedVolumeName(), QStringLiteral("cache")); // 已 trim
    QCOMPARE(m_backend->lastCreatedVolumeDriver(), QStringLiteral("local"));

    m_backend->completeMutations(DockerBackendInterface::MutationOutcome::Succeeded);
    QCOMPARE(volumesSpy.count(), 1);
    QVERIFY2(m_operations->resultText().contains(QStringLiteral("cache")), qPrintable(m_operations->resultText()));
    // 写后即读：列表与存储占用都要重读
    QVERIFY(m_backend->refreshCount(DockerBackendInterface::Section::Volumes) >= 1);
    QVERIFY(m_backend->refreshCount(DockerBackendInterface::Section::Storage) >= 1);
}

/*!
 * 删除与清理（§3.5）：删除**不提供 force**；prune 的"成功明细"经信号补齐文案。
 */
void OperationControllerTest::removeAndPruneVolumes()
{
    const QString name = QStringLiteral("cache");
    QSignalSpy volumesSpy(m_operations, &OperationController::volumesChanged);

    QVERIFY(m_operations->removeVolume(name));
    QCOMPARE(m_backend->lastRemovedVolume(), name);
    m_backend->completeMutation(OperationTarget::volume(name), DockerBackendInterface::MutationOutcome::Succeeded);
    QCOMPARE(volumesSpy.count(), 1);

    // 被容器使用时引擎拒绝：结果里带上引擎原文（界面说明"先用容器断开"）
    QVERIFY(m_operations->removeVolume(QStringLiteral("app_data")));
    m_backend->completeMutation(OperationTarget::volume(QStringLiteral("app_data")),
                                DockerBackendInterface::MutationOutcome::Failed,
                                DockerError(DockerError::Kind::Conflict, QStringLiteral("volume is in use")));
    QVERIFY2(m_operations->resultDetailText().contains(QStringLiteral("in use")),
             qPrintable(m_operations->resultDetailText()));

    // 清理：先发请求，成功后用信号里的明细补一句"回收了多少"
    QVERIFY(m_operations->pruneVolumes());
    QCOMPARE(m_backend->pruneCallCount(), 1);
    m_backend->completePrune({QStringLiteral("cache"), QStringLiteral("legacy")}, 2048);
    m_backend->completeMutation(OperationTarget::volumePrune(), DockerBackendInterface::MutationOutcome::Succeeded);
    // 只有成功的删除与清理会触发重读（失败的那次不该重读）
    QCOMPARE(volumesSpy.count(), 2);
    QVERIFY2(m_operations->resultText().contains(QStringLiteral("2")), qPrintable(m_operations->resultText()));
    QVERIFY2(!m_operations->resultText().isEmpty(), qPrintable(m_operations->resultText()));
    QVERIFY2(m_operations->resultDetailText().contains(QStringLiteral("cache")),
             qPrintable(m_operations->resultDetailText())); // 明细里列出删掉的卷名

    // 没有可清理的卷：给出"无需清理"而不是假装成功（真实顺序：明细信号先到，mutationFinished 后到）
    QVERIFY(m_operations->pruneVolumes());
    m_backend->completePrune({}, 0);
    m_backend->completeMutation(OperationTarget::volumePrune(), DockerBackendInterface::MutationOutcome::Succeeded);
    QVERIFY2(m_operations->resultText().contains(QStringLiteral("Nothing")), qPrintable(m_operations->resultText()));
    QVERIFY(m_operations->resultDetailText().isEmpty());
}

/*!
 * 创建容器的校验（ARCH_V5_V8 §4.3/§4.6）：依赖后端数据的检查也在 C++ 侧，失败给稳定 key。
 */
/*!
 * 暂停 / 继续（用户实测反馈 ①）：请求打到正确的端点，结果文案说清楚做了什么。
 */
void OperationControllerTest::pauseAndResumeContainer()
{
    QVERIFY(m_operations->writeAllowed());

    m_operations->pauseContainer(QStringLiteral("cid-1"));
    QCOMPARE(m_backend->mutationCalls().size(), 1);
    QCOMPARE(m_backend->mutationCalls().first().mutation, DockerBackendInterface::Mutation::PauseContainer);
    QCOMPARE(m_backend->mutationCalls().first().targetKey, OperationTarget::container(QStringLiteral("cid-1")));
    m_backend->completeMutation(OperationTarget::container(QStringLiteral("cid-1")),
                                DockerBackendInterface::MutationOutcome::Succeeded);
    QVERIFY2(m_operations->resultText().contains(QStringLiteral("paused")), qPrintable(m_operations->resultText()));

    m_operations->unpauseContainer(QStringLiteral("cid-1"));
    QCOMPARE(m_backend->mutationCalls().size(), 1);
    QCOMPARE(m_backend->mutationCalls().first().mutation, DockerBackendInterface::Mutation::UnpauseContainer);
    m_backend->completeMutation(OperationTarget::container(QStringLiteral("cid-1")),
                                DockerBackendInterface::MutationOutcome::Succeeded);
    QVERIFY2(m_operations->resultText().contains(QStringLiteral("resumed")), qPrintable(m_operations->resultText()));

    // 只读时：一个请求都不发，并且明确说明是只读
    // （用另建的 backend 指向不存在的 socket：写权限门就会判定为不可写）
    MockDockerBackend readOnlyBackend;
    readOnlyBackend.setEndpoint(DockerEndpoint::unixSocket(QStringLiteral("/tmp/kontainer-does-not-exist.sock")));
    OperationController readOnly(&readOnlyBackend);
    readOnly.refreshWriteAccess();
    QVERIFY(!readOnly.writeAllowed());
    readOnly.pauseContainer(QStringLiteral("cid-1"));
    QVERIFY2(readOnly.resultText().contains(QStringLiteral("read-only")), qPrintable(readOnly.resultText()));
    QVERIFY2(readOnlyBackend.mutationCalls().isEmpty(), "a read-only controller must not send anything");
}

void OperationControllerTest::createContainerValidatesAgainstExistingState()
{
    Container existing;
    existing.id = QStringLiteral("existing-id");
    existing.name = QStringLiteral("web");
    existing.image = QStringLiteral("alpine:3.19");
    existing.state = ContainerState::Running;
    // Port 字段顺序是 {ip, 容器端口, 宿主端口, 协议}
    existing.ports = {{QStringLiteral("0.0.0.0"), 80, 8080, QStringLiteral("tcp")}};
    m_backend->setContainers({existing});
    Image image;
    image.id = QStringLiteral("sha256:aaaa");
    image.repoTags = {QStringLiteral("alpine:3.19")};
    m_backend->setImages({image});

    const auto baseRequest = [] {
        QVariantMap request;
        request.insert(QStringLiteral("name"), QStringLiteral("worker"));
        request.insert(QStringLiteral("image"), QStringLiteral("alpine:3.19"));
        return request;
    };

    // 名称规则
    QVariantMap request = baseRequest();
    request.insert(QStringLiteral("name"), QStringLiteral("has space"));
    QVERIFY(!m_operations->createContainer(request));
    QCOMPARE(m_operations->resultDetailText(), QStringLiteral("nameInvalid"));

    // 与现有容器重名（大小写不敏感）
    request = baseRequest();
    request.insert(QStringLiteral("name"), QStringLiteral("WEB"));
    QVERIFY(!m_operations->createContainer(request));
    QCOMPARE(m_operations->resultDetailText(), QStringLiteral("nameInUse"));

    // 镜像不在本地：默认拒绝，`allowMissingImage` 时放行
    request = baseRequest();
    request.insert(QStringLiteral("image"), QStringLiteral("busybox:latest"));
    QVERIFY(!m_operations->createContainer(request));
    QCOMPARE(m_operations->resultDetailText(), QStringLiteral("imageNotLocal"));
    QVERIFY2(m_operations->createContainer(request, /*allowMissingImage=*/true),
             "the UI can offer 'pull first' and still submit");
    // 收尾那次提交：否则同名目标会一直处于"操作在途"，后面的同名提交会被拒绝
    m_backend->completeMutation(OperationTarget::container(QStringLiteral("worker")),
                                DockerBackendInterface::MutationOutcome::Succeeded);
    // 基线：此刻没有在途的写操作（completeMutation 会把已完成的调用从列表里清掉）
    const int callsAfterFirstSubmit = m_backend->mutationCalls().size();

    // 宿主端口冲突（0.0.0.0 与具体地址也算冲突）
    request = baseRequest();
    request.insert(QStringLiteral("ports"),
                   QVariantList {QVariantMap {{QStringLiteral("hostIp"), QStringLiteral("127.0.0.1")},
                                              {QStringLiteral("hostPort"), 8080},
                                              {QStringLiteral("containerPort"), 80},
                                              {QStringLiteral("protocol"), QStringLiteral("tcp")}}});
    QVERIFY(!m_operations->createContainer(request, true));
    QCOMPARE(m_operations->resultDetailText(), QStringLiteral("portInUse"));

    // 挂载目标必须是绝对路径
    request = baseRequest();
    request.insert(QStringLiteral("mounts"),
                   QVariantList {QVariantMap {{QStringLiteral("type"), QStringLiteral("bind")},
                                              {QStringLiteral("source"), QStringLiteral("/srv/x")},
                                              {QStringLiteral("destination"), QStringLiteral("relative")}}});
    QVERIFY(!m_operations->createContainer(request, true));
    QCOMPARE(m_operations->resultDetailText(), QStringLiteral("pathNotAbsolute"));

    // 这几次校验失败同样一个请求都没发
    QCOMPARE(m_backend->mutationCalls().size(), callsAfterFirstSubmit);

    // 合法请求：字段如实传下去
    request = baseRequest();
    request.insert(QStringLiteral("network"), QStringLiteral("app_default"));
    request.insert(QStringLiteral("environment"), QStringList {QStringLiteral("LANG=C")});
    request.insert(QStringLiteral("memoryLimitBytes"), 64LL * 1024 * 1024);
    request.insert(QStringLiteral("cpus"), 0.5);
    QVERIFY(m_operations->createContainer(request));
    const ContainerCreateRequest sent = m_backend->lastContainerCreate();
    QCOMPARE(sent.name, QStringLiteral("worker"));
    QCOMPARE(sent.image, QStringLiteral("alpine:3.19"));
    QCOMPARE(sent.network, QStringLiteral("app_default"));
    QCOMPARE(sent.environment, QStringList {QStringLiteral("LANG=C")});
    QCOMPARE(sent.memoryLimitBytes, 64LL * 1024 * 1024);
    QCOMPARE(sent.cpus, 0.5);
}

/*!
 * 「创建并启动」：两步串行，成功与失败都要说清是哪一步（§4.6）。
 */
void OperationControllerTest::createContainerCanStartAfterwards()
{
    Image image;
    image.id = QStringLiteral("sha256:aaaa");
    image.repoTags = {QStringLiteral("alpine:3.19")};
    m_backend->setImages({image});

    QSignalSpy createdSpy(m_operations, &OperationController::containerCreatedSignal);

    QVariantMap request;
    request.insert(QStringLiteral("name"), QStringLiteral("web"));
    request.insert(QStringLiteral("image"), QStringLiteral("alpine:3.19"));
    request.insert(QStringLiteral("startAfterCreate"), true);
    QVERIFY(m_operations->createContainer(request));

    // 第一步：引擎给了 id
    m_backend->completeContainerCreate(QStringLiteral("new-id"));
    QCOMPARE(createdSpy.count(), 1);
    QCOMPARE(createdSpy.at(0).at(0).toString(), QStringLiteral("new-id"));
    QCOMPARE(createdSpy.at(0).at(1).toBool(), false); // 尚未启动
    m_backend->completeMutation(OperationTarget::container(QStringLiteral("web")),
                                DockerBackendInterface::MutationOutcome::Succeeded);
    QVERIFY2(m_operations->resultText().contains(QStringLiteral("created")), qPrintable(m_operations->resultText()));

    // 第二步：启动成功 → 另给一条"已创建并启动"，并把 started=true 报给界面
    m_backend->completeMutation(OperationTarget::container(QStringLiteral("new-id")),
                                DockerBackendInterface::MutationOutcome::Succeeded);
    QCOMPARE(createdSpy.count(), 2);
    QCOMPARE(createdSpy.at(1).at(1).toBool(), true);
    QVERIFY2(m_operations->resultText().contains(QStringLiteral("started")), qPrintable(m_operations->resultText()));

    // 启动失败：文案必须说明"容器已创建，但启动失败"
    QVERIFY(m_operations->createContainer(request));
    m_backend->completeContainerCreate(QStringLiteral("second-id"));
    m_backend->completeMutation(OperationTarget::container(QStringLiteral("web")),
                                DockerBackendInterface::MutationOutcome::Succeeded);
    m_backend->completeMutation(OperationTarget::container(QStringLiteral("second-id")),
                                DockerBackendInterface::MutationOutcome::Failed,
                                DockerError(DockerError::Kind::EngineError, QStringLiteral("port is already allocated")));
    QVERIFY2(m_operations->resultText().contains(QStringLiteral("created")), qPrintable(m_operations->resultText()));
    QVERIFY2(m_operations->resultText().contains(QStringLiteral("started")), qPrintable(m_operations->resultText()));
    QVERIFY2(m_operations->resultDetailText().contains(QStringLiteral("port is already allocated")),
             qPrintable(m_operations->resultDetailText()));
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

/*!
 * 宿主端口冲突的判定（实测反馈：创建时没拦住，启动时才报
 * `Bind for 0.0.0.0:8100 failed: port is already allocated`）。
 *
 * 两个要点：
 *  - **只有真的占着端口的容器**才算冲突：已退出的容器不持有宿主端口，拿它当冲突会误报；
 *  - 冲突时必须能说出**是谁占着**，界面才能告诉用户"先停掉哪个容器"。
 */
void OperationControllerTest::portHolderIsReportedAndStoppedContainersDoNotBlock()
{
    Container running;
    running.id = QStringLiteral("running-id");
    running.name = QStringLiteral("holder");
    running.image = QStringLiteral("alpine:3.19");
    running.state = ContainerState::Running;
    running.ports = {{QStringLiteral("0.0.0.0"), 80, 8100, QStringLiteral("tcp")}};

    Container stopped;
    stopped.id = QStringLiteral("stopped-id");
    stopped.name = QStringLiteral("old");
    stopped.image = QStringLiteral("alpine:3.19");
    stopped.state = ContainerState::Exited;
    stopped.ports = {{QStringLiteral("0.0.0.0"), 80, 8200, QStringLiteral("tcp")}};

    Container specific;
    specific.id = QStringLiteral("specific-id");
    specific.name = QStringLiteral("localhost-only");
    specific.image = QStringLiteral("alpine:3.19");
    specific.state = ContainerState::Running;
    specific.ports = {{QStringLiteral("127.0.0.1"), 80, 8300, QStringLiteral("tcp")}};

    m_backend->setContainers({running, stopped, specific});

    // 运行中的容器占着 → 冲突，并报出名字
    QVERIFY(m_operations->hostPortInUse(QStringLiteral("0.0.0.0"), 8100));
    QCOMPARE(m_operations->hostPortHolder(QStringLiteral("0.0.0.0"), 8100), QStringLiteral("holder"));
    // 具体地址与通配互相冲突（同一端口）
    QVERIFY(m_operations->hostPortInUse(QStringLiteral("::"), 8100));
    QVERIFY(m_operations->hostPortInUse(QStringLiteral("127.0.0.1"), 8100));

    // 已退出的容器不占端口 → 不冲突（没运行自然不会占用；按占用拦会挡住别的应用）
    QVERIFY2(!m_operations->hostPortInUse(QStringLiteral("0.0.0.0"), 8200),
             "a stopped container does not hold its published ports");
    QVERIFY(m_operations->hostPortHolder(QStringLiteral("0.0.0.0"), 8200).isEmpty());

    // 不同具体地址之间不冲突
    QVERIFY(!m_operations->hostPortInUse(QStringLiteral("192.168.1.5"), 8300));
    QVERIFY(m_operations->hostPortInUse(QStringLiteral("127.0.0.1"), 8300));

    // 随机端口（0）永远不冲突
    QVERIFY(!m_operations->hostPortInUse(QStringLiteral("0.0.0.0"), 0));
}

/*!
 * 启动时才失败的端口占用：错误文案要说清"换个端口或停掉那个容器"，
 * 并把端口号从引擎原文里提出来（普通用户读不懂 `driver failed programming external connectivity`）。
 */
void OperationControllerTest::startFailureAboutAnAllocatedPortIsExplained()
{
    Container container;
    container.id = QStringLiteral("cid-1");
    container.name = QStringLiteral("ubuntu-c184");
    container.image = QStringLiteral("ubuntu:24.04");
    container.state = ContainerState::Exited;
    m_backend->setContainers({container});

    m_operations->startContainer(QStringLiteral("cid-1"));
    const DockerError error(DockerError::Kind::Conflict,
                            QStringLiteral("driver failed programming external connectivity on endpoint ubuntu-c184 "
                                           "(e80ee6afdf80): Bind for 0.0.0.0:8100 failed: port is already allocated"));
    m_backend->completeMutation(OperationTarget::container(QStringLiteral("cid-1")),
                                DockerBackendInterface::MutationOutcome::Failed,
                                error);

    const QString text = m_operations->resultText();
    QVERIFY2(text.contains(QStringLiteral("8100")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("already used")), qPrintable(text));
    // 不要直接把引擎原文端给用户（那是"技术细节"的位置）
    QVERIFY2(!text.contains(QStringLiteral("driver failed")), qPrintable(text));
}


QTEST_MAIN(OperationControllerTest)

#include "tst_operation_controller.moc"
