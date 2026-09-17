/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "i18n.h"
#include "model/container_detail_controller.h"
#include "model/detail_list_model.h"
#include "model/image_detail_controller.h"
#include "support/mock_docker_backend.h"

#include <QSignalSpy>
#include <QtTest>

using namespace Kontainer;

/*!
 * 详情页 controller 测试（ARCH_V2 §27/§28/§31/§43/§46）。
 *
 * 重点验证生命周期：页面进入才请求数据、离开就停止采样并释放历史；
 * 以及 inspect 失败时列表页不受影响（错误只落在详情分区）。
 */
class DetailControllersTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void containerDetailStartsIdle();
    void containerDetailLoadsAndBuildsLists();
    void containerDetailStartsMetricsOnlyWhenRunning();
    void containerDetailStopReleasesSampling();
    void containerDetailReloadsAfterReentry();
    void containerDetailReportsErrorAndRetries();
    void imageDetailLoadsTagsLayersAndUsage();
    void imageDetailReportsError();
    void unchangedDetailListsDoNotResetTheModel();
};

namespace
{

ContainerDetail makeDetail(ContainerState state)
{
    ContainerDetail detail;
    detail.id = QStringLiteral("cid-1");
    detail.name = QStringLiteral("demo");
    detail.image = QStringLiteral("alpine:latest");
    detail.imageId = QStringLiteral("sha256:aaaa");
    detail.state = state;
    detail.health = HealthState::Healthy;
    detail.status = QStringLiteral("Up 2 hours");
    detail.created = QDateTime::currentDateTimeUtc().addSecs(-7200);
    detail.started = QDateTime::currentDateTimeUtc().addSecs(-7000);
    detail.restartCount = 2;
    detail.environment = {QStringLiteral("PATH=/usr/bin"), QStringLiteral("LANG=C")};
    detail.command = {QStringLiteral("sleep"), QStringLiteral("infinity")};
    detail.entrypoint = {QStringLiteral("/entry.sh")};
    detail.workingDirectory = QStringLiteral("/work");
    detail.ports.append(Port {QStringLiteral("0.0.0.0"), 8080, 18080, QStringLiteral("tcp")});
    detail.networks.append(ContainerNetwork {QStringLiteral("bridge"),
                                            QStringLiteral("net1"),
                                            QStringLiteral("172.17.0.5"),
                                            QString(),
                                            QStringLiteral("02:42:ac:11:00:05"),
                                            QStringLiteral("172.17.0.1")});
    detail.mounts.append(ContainerMount {QStringLiteral("bind"), QStringLiteral("/host/data"), QStringLiteral("/data"), QStringLiteral("rw"), false});
    detail.labels.append({QStringLiteral("com.example.role"), QStringLiteral("test")});
    return detail;
}

} // namespace

void DetailControllersTest::initTestCase()
{
    setupTranslationDomain();
}

void DetailControllersTest::containerDetailStartsIdle()
{
    MockDockerBackend backend;
    ContainerDetailController controller(&backend);

    QCOMPARE(controller.loadStateKey(), QStringLiteral("idle"));
    QVERIFY(!controller.hasDetail());
    QVERIFY(controller.errorText().isEmpty());
}

void DetailControllersTest::containerDetailLoadsAndBuildsLists()
{
    MockDockerBackend backend;
    backend.setContainerDetail(makeDetail(ContainerState::Running));

    ContainerDetailController controller(&backend);
    controller.setContainerId(QStringLiteral("cid-1"));
    controller.start();
    QCOMPARE(controller.loadStateKey(), QStringLiteral("loading"));

    backend.completeRefresh();
    QCOMPARE(controller.loadStateKey(), QStringLiteral("ready"));
    QVERIFY(controller.hasDetail());
    QCOMPARE(controller.name(), QStringLiteral("demo"));
    QCOMPARE(controller.stateKey(), QStringLiteral("running"));
    QCOMPARE(controller.healthKey(), QStringLiteral("healthy"));
    QCOMPARE(controller.restartCount(), 2);
    QCOMPARE(controller.environmentCount(), 2);

    // 结构化子列表（不是把 JSON 丢给 QML）
    QCOMPARE(controller.ports()->count(), 1);
    QCOMPARE(controller.ports()->index(0, 0).data(DetailListModel::LabelRole).toString(), QStringLiteral("8080/tcp"));
    QCOMPARE(controller.ports()->index(0, 0).data(DetailListModel::ValueRole).toString(), QStringLiteral("18080"));
    QCOMPARE(controller.networks()->index(0, 0).data(DetailListModel::LabelRole).toString(), QStringLiteral("bridge"));
    QCOMPARE(controller.networks()->index(0, 0).data(DetailListModel::ValueRole).toString(), QStringLiteral("172.17.0.5"));
    QCOMPARE(controller.mounts()->index(0, 0).data(DetailListModel::LabelRole).toString(), QStringLiteral("/data"));
    QCOMPARE(controller.mounts()->index(0, 0).data(DetailListModel::ValueRole).toString(), QStringLiteral("/host/data"));
    QCOMPARE(controller.labels()->count(), 1);
    QCOMPARE(controller.environmentVariables()->count(), 2);
    QCOMPARE(controller.environmentVariables()->index(0, 0).data(DetailListModel::LabelRole).toString(), QStringLiteral("PATH"));
}

void DetailControllersTest::containerDetailStartsMetricsOnlyWhenRunning()
{
    MockDockerBackend backend;
    backend.setContainerDetail(makeDetail(ContainerState::Exited));

    ContainerDetailController controller(&backend);
    controller.setContainerId(QStringLiteral("cid-1"));
    controller.start();
    backend.completeRefresh();

    // 已停止容器没有资源数据：不应该轮询 stats（§17/§27）
    QVERIFY(!controller.running());
    QVERIFY(!controller.metrics()->sampling());
    QVERIFY(backend.samplingIds().isEmpty());
}

void DetailControllersTest::containerDetailStopReleasesSampling()
{
    MockDockerBackend backend;
    backend.setContainerDetail(makeDetail(ContainerState::Running));

    ContainerDetailController controller(&backend);
    controller.setContainerId(QStringLiteral("cid-1"));
    controller.start();
    backend.completeRefresh();
    QVERIFY(controller.metrics()->sampling());
    QVERIFY(backend.samplingIds().contains(QStringLiteral("cid-1")));

    // 离开页面（§27）
    controller.stop();
    QVERIFY(!controller.metrics()->sampling());
    QVERIFY(backend.samplingIds().isEmpty());
    QCOMPARE(controller.metrics()->sampleCount(), 0);
}

/*!
 * §27/§46：离开详情页再进入同一个对象，必须重新 inspect 并恢复 stats 采样
 * （否则页面会一直显示旧缓存，Resources 卡在 “Not sampling”）。
 */
void DetailControllersTest::containerDetailReloadsAfterReentry()
{
    MockDockerBackend backend;
    backend.setContainerDetail(makeDetail(ContainerState::Running));

    ContainerDetailController controller(&backend);
    controller.setContainerId(QStringLiteral("cid-1"));

    controller.start();
    backend.completeRefresh();
    QCOMPARE(controller.loadStateKey(), QStringLiteral("ready"));
    QVERIFY(controller.metrics()->sampling());

    controller.stop();
    QVERIFY(!controller.metrics()->sampling());
    QVERIFY(backend.samplingIds().isEmpty());

    // 再次进入同一个容器
    const int inspectBefore = backend.refreshCount(DockerBackendInterface::Section::ContainerDetail);
    controller.start();
    backend.completeRefresh();

    QCOMPARE(backend.refreshCount(DockerBackendInterface::Section::ContainerDetail), inspectBefore + 1);
    QCOMPARE(controller.loadStateKey(), QStringLiteral("ready"));
    QVERIFY(controller.metrics()->sampling());
    QVERIFY(backend.samplingIds().contains(QStringLiteral("cid-1")));
}

void DetailControllersTest::containerDetailReportsErrorAndRetries()
{
    MockDockerBackend backend;
    backend.setNextFailure(DockerBackendInterface::Section::ContainerDetail,
                           DockerError(DockerError::Kind::NotFound, QStringLiteral("no such container")));

    ContainerDetailController controller(&backend);
    controller.setContainerId(QStringLiteral("missing"));
    controller.start();
    backend.completeRefresh();

    QCOMPARE(controller.loadStateKey(), QStringLiteral("error"));
    QVERIFY(!controller.errorText().isEmpty());

    // 重试成功后进入 ready（§31）
    backend.setContainerDetail(makeDetail(ContainerState::Running));
    controller.refresh();
    QCOMPARE(controller.loadStateKey(), QStringLiteral("loading"));
    backend.completeRefresh();
    QCOMPARE(controller.loadStateKey(), QStringLiteral("ready"));
}

void DetailControllersTest::imageDetailLoadsTagsLayersAndUsage()
{
    MockDockerBackend backend;

    ImageDetail detail;
    detail.id = QStringLiteral("sha256:aaaa");
    detail.repoTags = {QStringLiteral("ghcr.io/dockur/windows:6.05"), QStringLiteral("windows:latest")};
    detail.repoDigests = {QStringLiteral("ghcr.io/dockur/windows@sha256:bbbb")};
    detail.created = QDateTime::currentDateTimeUtc().addDays(-3);
    detail.sizeBytes = 800733572;
    detail.architecture = QStringLiteral("amd64");
    detail.os = QStringLiteral("linux");
    detail.layers = {QStringLiteral("sha256:1111"), QStringLiteral("sha256:2222")};
    detail.environment = {QStringLiteral("PATH=/usr/bin")};
    backend.setImageDetail(detail);

    Container usingImage;
    usingImage.id = QStringLiteral("cid-1");
    usingImage.name = QStringLiteral("WinBoat");
    usingImage.imageId = QStringLiteral("sha256:aaaa");
    usingImage.state = ContainerState::Exited;
    Container other;
    other.id = QStringLiteral("cid-2");
    other.name = QStringLiteral("other");
    other.imageId = QStringLiteral("sha256:cccc");
    backend.setContainers({usingImage, other});

    ImageDetailController controller(&backend);
    controller.setImageId(QStringLiteral("sha256:aaaa"));
    controller.start();
    backend.completeRefresh();

    QCOMPARE(controller.loadStateKey(), QStringLiteral("ready"));
    QCOMPARE(controller.primaryRepository(), QStringLiteral("ghcr.io/dockur/windows"));
    QCOMPARE(controller.tagName(), QStringLiteral("6.05"));
    QCOMPARE(controller.primaryTag(), QStringLiteral("ghcr.io/dockur/windows:6.05"));
    QCOMPARE(controller.layerCount(), 2);
    QCOMPARE(controller.tags()->count(), 2);
    QCOMPARE(controller.digests()->count(), 1);
    QCOMPARE(controller.layers()->index(0, 0).data(DetailListModel::LabelRole).toString(), QStringLiteral("1"));
    QCOMPARE(controller.environmentCount(), 1);

    // §52：与 Container 的只读关联
    QCOMPARE(controller.usedByContainers()->count(), 1);
    QCOMPARE(controller.usedByContainers()->index(0, 0).data(DetailListModel::LabelRole).toString(), QStringLiteral("WinBoat"));
}

void DetailControllersTest::imageDetailReportsError()
{
    MockDockerBackend backend;
    backend.setNextFailure(DockerBackendInterface::Section::ImageDetail,
                           DockerError(DockerError::Kind::NotFound, QStringLiteral("no such image")));

    ImageDetailController controller(&backend);
    controller.setImageId(QStringLiteral("missing"));
    controller.start();
    backend.completeRefresh();

    QCOMPARE(controller.loadStateKey(), QStringLiteral("error"));
    QVERIFY(!controller.errorText().isEmpty());
}

/*!
 * ARCH_V2 §32/§34 + ARCH_V3 附录 A.1d：
 * 静默刷新（数据没变）时详情列表**不得**重置模型。
 *
 * 详情页的列表会被反复重建：容器列表每 5 秒变化一次就会触发
 * ImageDetailController::rebuildUsedBy()，inspect 复核每 30 秒触发
 * ContainerDetailController::rebuildLists()。如果每次都发 modelReset，
 * QML 里的 Repeater 就会反复销毁重建 delegate，而「布局正在算尺寸时条目被销毁」
 * 正是真实会话里段错误（QGridLayoutEngine / polish）的触发条件。
 */
void DetailControllersTest::unchangedDetailListsDoNotResetTheModel()
{
    MockDockerBackend backend;

    ContainerDetail detail = makeDetail(ContainerState::Running);
    detail.networks = {{QStringLiteral("bridge"), QStringLiteral("id"), QStringLiteral("172.17.0.2"), {}, {}, QStringLiteral("172.17.0.1")}};
    detail.mounts = {{QStringLiteral("bind"), QStringLiteral("/srv/data"), QStringLiteral("/data"), QStringLiteral("rw"), false}};
    backend.setContainerDetail(detail);

    ContainerDetailController controller(&backend);
    controller.setContainerId(QStringLiteral("cid-1"));
    controller.start();
    backend.completeRefresh();

    // 一个 Docker 网络可能展开成多条展示条目（network / ipv6 / gateway），
    // 因此这里只固定「初始条数」，不假设它与 Docker 侧条目数一一对应
    const int initialNetworkCount = controller.networks()->count();
    const int initialMountCount = controller.mounts()->count();
    QVERIFY(initialNetworkCount > 0);
    QVERIFY(initialMountCount > 0);

    QSignalSpy networksReset(controller.networks(), &QAbstractItemModel::modelReset);
    QSignalSpy mountsReset(controller.mounts(), &QAbstractItemModel::modelReset);
    QSignalSpy countChanged(controller.networks(), &DetailListModel::countChanged);

    // 连续三次「数据完全没变」的 inspect 复核
    for (int round = 0; round < 3; ++round) {
        controller.refresh();
        backend.completeRefresh();
    }
    QCOMPARE(networksReset.count(), 0);
    QCOMPARE(mountsReset.count(), 0);
    QCOMPARE(countChanged.count(), 0);

    // 数据真的变了就必须重置，否则界面会显示过期内容
    detail.networks.append({QStringLiteral("host"), QStringLiteral("id2"), QStringLiteral("172.17.0.3"), {}, {}, QStringLiteral("172.17.0.1")});
    backend.setContainerDetail(detail);
    controller.refresh();
    backend.completeRefresh();
    QCOMPARE(networksReset.count(), 1);
    QVERIFY(controller.networks()->count() > initialNetworkCount);
}

QTEST_GUILESS_MAIN(DetailControllersTest)

#include "tst_detail_controllers.moc"
