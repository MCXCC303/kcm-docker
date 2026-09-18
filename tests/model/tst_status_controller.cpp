/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "i18n.h"
#include "model/docker_error_text.h"
#include "refresh_policy.h"
#include "model/status_controller.h"
#include "support/mock_docker_backend.h"

#include <QtTest>

using namespace Kontainer;

/*! UI 状态机与错误隔离测试（ARCH_V1 §14/§15/§16，使用 mock backend §30）。 */
class StatusControllerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void startsIdle();
    void manualRefreshLoadsAllSections();
    void becomesReadyAfterSuccessfulRefresh();
    void errorIsolationKeepsWorkingSections();
    void allFailuresLeadToErrorState();
    void sectionErrorsAreClearedByLaterSuccess();
    void modelsAreFilledFromBackend();
    void endpointComesFromBackend();
    void autoRefreshIsLowFrequencyAndCanBeDisabled();
    void exposesExplicitSectionStates();
    void emptyListShowsEmptyStateNotError();
    void reportsPartialEngineStateWhenSummaryIsMissing();
    void tracksLastSuccessfulUpdate();
    void failedRefreshDoesNotStayInLoading();
    void manualRefreshClearsTheStickyFailureFlag();
    void watchdogAbandonsStuckRequests();
    void serviceStatesShapeTheConnectionKey();
    void retryStorageOnlyRefreshesStorage();
};

void StatusControllerTest::initTestCase()
{
    setupTranslationDomain();
}

void StatusControllerTest::startsIdle()
{
    MockDockerBackend backend;
    StatusController controller(&backend);

    QCOMPARE(int(controller.state()), int(StatusController::State::Idle));
    QVERIFY(!controller.busy());
    QVERIFY(controller.engineError().isEmpty());
    QVERIFY(controller.containersError().isEmpty());
    QVERIFY(controller.imagesError().isEmpty());
}

void StatusControllerTest::manualRefreshLoadsAllSections()
{
    MockDockerBackend backend;
    StatusController controller(&backend);

    controller.refresh();

    QCOMPARE(backend.refreshCount(DockerBackendInterface::Section::Engine), 1);
    QCOMPARE(backend.refreshCount(DockerBackendInterface::Section::Containers), 1);
    QCOMPARE(backend.refreshCount(DockerBackendInterface::Section::Images), 1);
    QCOMPARE(int(controller.state()), int(StatusController::State::Loading));
    QVERIFY(controller.busy());
}

void StatusControllerTest::becomesReadyAfterSuccessfulRefresh()
{
    MockDockerBackend backend;
    EngineInfo info;
    info.available = true;
    info.countsAvailable = true;
    info.serverVersion = QStringLiteral("29.8.0");
    backend.setEngineInfo(info);

    StatusController controller(&backend);
    QSignalSpy stateSpy(&controller, &StatusController::stateChanged);

    controller.refresh();
    QCOMPARE(int(controller.state()), int(StatusController::State::Loading));

    backend.completeRefresh();

    QCOMPARE(int(controller.state()), int(StatusController::State::Ready));
    QVERIFY(!controller.busy());
    QCOMPARE(controller.engine()->serverVersion(), QStringLiteral("29.8.0"));
    QVERIFY(controller.engine()->available());
    QVERIFY(stateSpy.count() >= 2);
}

void StatusControllerTest::errorIsolationKeepsWorkingSections()
{
    MockDockerBackend backend;
    backend.setNextFailure(DockerBackendInterface::Section::Engine,
                           DockerError(DockerError::Kind::PermissionDenied, QStringLiteral("EACCES")));

    Container container;
    container.id = QStringLiteral("abc123");
    container.name = QStringLiteral("demo");
    container.state = ContainerState::Running;
    backend.setContainers({container});

    StatusController controller(&backend);
    controller.refresh();
    backend.completeRefresh();

    // 容器仍然可用 → 不能整页不可用（§15）
    QCOMPARE(int(controller.state()), int(StatusController::State::Ready));
    QCOMPARE(controller.containers()->count(), 1);
    QVERIFY(!controller.containersError().isEmpty() == false);
    QVERIFY(!controller.engineError().isEmpty());
    QCOMPARE(controller.engineError(), dockerErrorText(DockerError(DockerError::Kind::PermissionDenied)));
    QVERIFY(!controller.engine()->available());
}

void StatusControllerTest::allFailuresLeadToErrorState()
{
    MockDockerBackend backend;
    const DockerError error(DockerError::Kind::DockerUnavailable, QStringLiteral("socket missing"));
    backend.setNextFailure(DockerBackendInterface::Section::Engine, error);
    backend.setNextFailure(DockerBackendInterface::Section::Containers, error);
    backend.setNextFailure(DockerBackendInterface::Section::Images, error);

    StatusController controller(&backend);
    QSignalSpy stateSpy(&controller, &StatusController::stateChanged);

    controller.refresh();
    backend.completeRefresh();

    QCOMPARE(int(controller.state()), int(StatusController::State::Error));
    QCOMPARE(controller.stateKey(), QStringLiteral("error"));
    QCOMPARE(controller.engineStateKey(), QStringLiteral("unavailable"));
    QCOMPARE(controller.containersStateKey(), QStringLiteral("error"));
    QCOMPARE(controller.imagesStateKey(), QStringLiteral("error"));
    QVERIFY(!controller.engineError().isEmpty());
    QVERIFY(!controller.containersError().isEmpty());
    QVERIFY(!controller.imagesError().isEmpty());
    QCOMPARE(controller.engineError(), dockerErrorText(error));
}

void StatusControllerTest::sectionErrorsAreClearedByLaterSuccess()
{
    MockDockerBackend backend;
    backend.setNextFailure(DockerBackendInterface::Section::Containers,
                           DockerError(DockerError::Kind::EngineError, QStringLiteral("500")));

    StatusController controller(&backend);
    controller.refresh();
    backend.completeRefresh();
    QVERIFY(!controller.containersError().isEmpty());

    // 下一次成功必须清掉错误
    controller.refresh();
    backend.completeRefresh();
    QVERIFY(controller.containersError().isEmpty());
    QCOMPARE(int(controller.state()), int(StatusController::State::Ready));
}

void StatusControllerTest::modelsAreFilledFromBackend()
{
    MockDockerBackend backend;

    Container first;
    first.id = QStringLiteral("aaa");
    first.name = QStringLiteral("first");
    Container second;
    second.id = QStringLiteral("bbb");
    second.name = QStringLiteral("second");
    backend.setContainers({first, second});

    Image image;
    image.id = QStringLiteral("sha256:ccc");
    image.repoTags = {QStringLiteral("demo:latest")};
    backend.setImages({image});

    StatusController controller(&backend);
    controller.refresh();
    backend.completeRefresh();

    QCOMPARE(controller.containers()->count(), 2);
    QCOMPARE(controller.images()->count(), 1);
    QCOMPARE(controller.containers()->data(controller.containers()->index(1, 0), ContainerModel::NameRole).toString(),
             QStringLiteral("second"));
    QCOMPARE(controller.images()->data(controller.images()->index(0, 0), ImageModel::PrimaryTagRole).toString(),
             QStringLiteral("demo:latest"));
}

void StatusControllerTest::endpointComesFromBackend()
{
    MockDockerBackend backend;
    backend.setEndpointName(QStringLiteral("unix:///run/docker.sock"));

    StatusController controller(&backend);
    QCOMPARE(controller.endpoint(), QStringLiteral("unix:///run/docker.sock"));
}

void StatusControllerTest::autoRefreshIsLowFrequencyAndCanBeDisabled()
{
    MockDockerBackend backend;
    StatusController controller(&backend);

    // ARCH_V2 §13.1：默认 5 秒；间隔集中定义在 RefreshPolicy，测试也不再写 magic number
    const int expectedFastMs = int(std::chrono::duration_cast<std::chrono::milliseconds>(RefreshPolicy::kDefaultRefreshInterval).count());
    const int expectedStorageMs = int(std::chrono::duration_cast<std::chrono::milliseconds>(RefreshPolicy::kStorageRefreshInterval).count());
    QCOMPARE(controller.autoRefreshInterval(), expectedFastMs);
    QCOMPARE(controller.storageRefreshInterval(), expectedStorageMs);
    QVERIFY(controller.autoRefreshEnabled());

    QSignalSpy spy(&controller, &StatusController::autoRefreshEnabledChanged);
    controller.setAutoRefreshEnabled(false);
    QCOMPARE(spy.count(), 1);
    QVERIFY(!controller.autoRefreshEnabled());

    controller.setAutoRefreshEnabled(true);
    QVERIFY(controller.autoRefreshEnabled());
}


/*!
 * §14：QML 不得用多个布尔拼装状态，因此每个数据集都必须能拿到单一枚举状态。
 */
void StatusControllerTest::exposesExplicitSectionStates()
{
    MockDockerBackend backend;
    EngineInfo info;
    info.available = true;
    info.countsAvailable = true;
    backend.setEngineInfo(info);

    Container container;
    container.id = QStringLiteral("abc");
    backend.setContainers({container});

    StatusController controller(&backend);

    // 首次刷新发起前：Engine 处于 Loading，而不是"未连接"
    QCOMPARE(int(controller.engineState()), int(StatusController::EngineState::Loading));
    QCOMPARE(int(controller.containersState()), int(StatusController::ListState::Idle));
    QCOMPARE(int(controller.imagesState()), int(StatusController::ListState::Idle));
    QCOMPARE(controller.stateKey(), QStringLiteral("idle"));
    QCOMPARE(controller.engineStateKey(), QStringLiteral("loading"));
    QCOMPARE(controller.containersStateKey(), QStringLiteral("idle"));

    controller.refresh();
    QCOMPARE(int(controller.engineState()), int(StatusController::EngineState::Loading));
    QCOMPARE(int(controller.containersState()), int(StatusController::ListState::Loading));
    QCOMPARE(int(controller.imagesState()), int(StatusController::ListState::Loading));
    QCOMPARE(controller.stateKey(), QStringLiteral("loading"));
    QCOMPARE(controller.containersStateKey(), QStringLiteral("loading"));

    backend.completeRefresh();
    QCOMPARE(int(controller.engineState()), int(StatusController::EngineState::Ready));
    QCOMPARE(int(controller.containersState()), int(StatusController::ListState::Ready));
    QCOMPARE(int(controller.imagesState()), int(StatusController::ListState::Empty)); // 请求成功但列表为空
    // QML 契约：状态 key 必须稳定（QML 通过这些字符串判断展示）
    QCOMPARE(controller.stateKey(), QStringLiteral("ready"));
    QCOMPARE(controller.engineStateKey(), QStringLiteral("ready"));
    QCOMPARE(controller.containersStateKey(), QStringLiteral("ready"));
    QCOMPARE(controller.imagesStateKey(), QStringLiteral("empty"));

    // 已有数据时再次刷新 → refreshing（UI 显示 Connected + 转圈）
    controller.refresh();
    QCOMPARE(controller.engineStateKey(), QStringLiteral("refreshing"));
    backend.completeRefresh();
    QCOMPARE(controller.engineStateKey(), QStringLiteral("ready"));
}

void StatusControllerTest::emptyListShowsEmptyStateNotError()
{
    MockDockerBackend backend;
    EngineInfo info;
    info.available = true;
    info.countsAvailable = true;
    backend.setEngineInfo(info);
    backend.setContainers({}); // 空列表不是错误（§38）

    StatusController controller(&backend);
    controller.refresh();
    backend.completeRefresh();

    QCOMPARE(int(controller.containersState()), int(StatusController::ListState::Empty));
    QCOMPARE(controller.containersStateKey(), QStringLiteral("empty"));
    QVERIFY(controller.containersError().isEmpty());
    QCOMPARE(int(controller.state()), int(StatusController::State::Ready));
}

void StatusControllerTest::reportsPartialEngineStateWhenSummaryIsMissing()
{
    MockDockerBackend backend;
    EngineInfo info;
    info.available = true; // /_ping 与 /version 成功
    info.countsAvailable = false; // 但 /info 失败
    info.serverVersion = QStringLiteral("29.8.0");
    backend.setEngineInfo(info);
    backend.setNextFailure(DockerBackendInterface::Section::Engine,
                           DockerError(DockerError::Kind::EngineError, QStringLiteral("no summary")));

    StatusController controller(&backend);
    controller.refresh();
    backend.completeRefresh();

    QCOMPARE(int(controller.engineState()), int(StatusController::EngineState::Partial));
    QCOMPARE(controller.engineStateKey(), QStringLiteral("partial"));
    QVERIFY(controller.engine()->available());
    QVERIFY(!controller.engine()->countsAvailable());
    QVERIFY(!controller.engineError().isEmpty());
}

/*!
 * §15：Last Updated 必须反映真实的成功更新时间，而不是刷新尝试时间。
 */
void StatusControllerTest::tracksLastSuccessfulUpdate()
{
    MockDockerBackend backend;
    StatusController controller(&backend);

    QVERIFY(!controller.lastUpdated().isValid());
    QVERIFY(!controller.updateFailed());
    QVERIFY(!controller.stale());

    controller.refresh();
    QVERIFY(!controller.lastUpdated().isValid()); // 还没成功

    backend.completeRefresh();
    QVERIFY(controller.lastUpdated().isValid());
    QVERIFY(!controller.updateFailed());

    /*
     * 连续失败达到阈值 → stale（§16）。
     *
     * 注意：这里必须走**自动**刷新路径。手动刷新会清零失败计数（B2 的修复：
     * 服务恢复后手动刷新不该继续显示"更新失败"），所以用 refresh() 累加失败是测不到 stale 的——
     * 这一点本身也是那条修复的断言（见 manualRefreshClearsTheStickyFailureFlag）。
     */
    const DockerError failure(DockerError::Kind::EngineError, QStringLiteral("boom"));
    for (int i = 0; i < RefreshPolicy::kStaleAfterFailedCycles; ++i) {
        backend.setNextFailure(DockerBackendInterface::Section::Containers, failure);
        controller.requestAutomaticRefreshForTesting();
        backend.completeRefresh();
    }
    QVERIFY(controller.updateFailed());
    QVERIFY(controller.stale());
    QVERIFY(controller.lastUpdated().isValid()); // 旧的成功时间仍然保留
}

/*!
 * §30/§55：storage 失败后的重试入口必须只重试该数据集，
 * 而且 QML 不能直接访问 backend（§4/§43），所以走 controller 的显式方法。
 */
void StatusControllerTest::retryStorageOnlyRefreshesStorage()
{
    MockDockerBackend backend;
    StatusController controller(&backend);

    const int containersBefore = backend.refreshCount(DockerBackendInterface::Section::Containers);
    const int engineBefore = backend.refreshCount(DockerBackendInterface::Section::Engine);

    controller.retryStorage();

    QCOMPARE(backend.refreshCount(DockerBackendInterface::Section::Storage), 1);
    QCOMPARE(backend.refreshCount(DockerBackendInterface::Section::Containers), containersBefore);
    QCOMPARE(backend.refreshCount(DockerBackendInterface::Section::Engine), engineBefore);
}

/*!
 * B3：一次都没成功过、但已经尝试过并失败时，引擎状态必须是"不可用"而不是永远"正在加载"。
 */
void StatusControllerTest::failedRefreshDoesNotStayInLoading()
{
    MockDockerBackend backend;
    const DockerError error(DockerError::Kind::DockerUnavailable, QStringLiteral("Cannot connect to the Docker daemon"));
    backend.setNextFailure(DockerBackendInterface::Section::Engine, error);
    backend.setNextFailure(DockerBackendInterface::Section::Containers, error);
    backend.setNextFailure(DockerBackendInterface::Section::Images, error);

    StatusController controller(&backend);
    QCOMPARE(controller.engineStateKey(), QStringLiteral("loading")); // 还没请求过：加载中是合理的

    controller.refresh();
    backend.completeRefresh();

    QVERIFY2(!controller.busy(), "a finished (failed) refresh must not keep the busy flag");
    QCOMPARE(controller.engineStateKey(), QStringLiteral("unavailable"));
    QVERIFY2(!controller.engineError().isEmpty(), "the reason must reach the UI");
}

/*!
 * B2：手动刷新是一次"重新开始"——上一次的失败标记不能粘住。
 */
void StatusControllerTest::manualRefreshClearsTheStickyFailureFlag()
{
    MockDockerBackend backend;
    const DockerError error(DockerError::Kind::DockerUnavailable, QStringLiteral("service stopped"));
    backend.setNextFailure(DockerBackendInterface::Section::Engine, error);
    backend.setNextFailure(DockerBackendInterface::Section::Containers, error);
    backend.setNextFailure(DockerBackendInterface::Section::Images, error);

    StatusController controller(&backend);
    controller.refresh();
    backend.completeRefresh();
    QVERIFY2(controller.updateFailed(), "the first cycle failed, so the flag must be set");

    // 服务恢复后用户点"刷新"：标记先清掉，再按本轮结果重算
    controller.refresh();
    QVERIFY2(!controller.updateFailed(), "a manual refresh restarts the failure accounting");
    backend.completeRefresh();
}

/*!
 * B4：请求卡住（永远不回来）时，看门狗必须放弃在途请求，让界面回到可重试的状态。
 */
void StatusControllerTest::watchdogAbandonsStuckRequests()
{
    MockDockerBackend backend;
    StatusController controller(&backend);
    // 用例里把看门狗压到 50ms（默认 20 秒，测试不能等）
    controller.setInFlightWatchdogMs(50);
    QCOMPARE(controller.inFlightWatchdogMs(), 50);

    backend.setStallRequests(true); // daemon 半死不活：socket 接了但不回数据
    controller.refresh();
    QVERIFY2(controller.busy(), "the refresh is in flight");

    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 3000);
    QCOMPARE(controller.engineStateKey(), QStringLiteral("unavailable"));
    QVERIFY2(!controller.engineError().isEmpty(), "the timeout must be reported as a reason");

    // 服务恢复后可以重新刷新（不会因为上一次被放弃而卡住）
    backend.setStallRequests(false);
    controller.refresh();
    backend.completeRefresh();
    QVERIFY2(!controller.busy(), "a later refresh must work again");
}

/*!
 * B1：服务状态决定"已连接"的说法——socket 在、服务停了时不能只说"已连接"。
 */
void StatusControllerTest::serviceStatesShapeTheConnectionKey()
{
    MockDockerBackend backend;
    EngineInfo info;
    info.available = true;
    info.countsAvailable = true;
    info.serverVersion = QStringLiteral("29.8.0");
    backend.setEngineInfo(info);
    FakeServiceStatus services;
    StatusController controller(&backend, nullptr, nullptr, nullptr, nullptr, nullptr, &services);

    // 三个 unit 都在运行 + 刷新成功 → 已连接
    controller.refresh();
    backend.completeRefresh();
    QCOMPARE(controller.connectionKey(), QStringLiteral("connected"));

    // docker.service 停了（socket 还在：正是用户遇到的场景）→ 必须提示"服务未运行"
    services.setUnitState(QStringLiteral("docker.service"), QStringLiteral("inactive"));
    QCOMPARE(controller.connectionKey(), QStringLiteral("connectedServicesDown"));

    // 服务全部停掉：即使还留着上一次读到的引擎数据，也只能说"服务未运行"
    services.setUnitState(QStringLiteral("docker.socket"), QStringLiteral("inactive"));
    QCOMPARE(controller.connectionKey(), QStringLiteral("connectedServicesDown"));

    /*
     * 从未连上过（引擎数据不可用）+ 服务未运行 → "未连接 + 服务未运行"。
     *
     * 注意：控制器会**保留**上一次读到的引擎数据（错误隔离），所以这里必须用一个
     * 全新的控制器，而不是把老控制器的引擎数据清掉——后者不会发生（也不该发生）。
     */
    {
        MockDockerBackend freshBackend;
        FakeServiceStatus freshServices;
        freshServices.setUnitState(QStringLiteral("docker.socket"), QStringLiteral("inactive"));
        freshServices.setUnitState(QStringLiteral("docker.service"), QStringLiteral("inactive"));
        StatusController fresh(&freshBackend, nullptr, nullptr, nullptr, nullptr, nullptr, &freshServices);
        fresh.refresh();
        freshBackend.completeRefresh();
        QCOMPARE(fresh.connectionKey(), QStringLiteral("disconnectedServicesDown"));
    }

    // 服务恢复：手里还有上一次读到的引擎数据，可以如实说"已连接"（刷新正在进行）
    services.setUnitState(QStringLiteral("docker.socket"), QStringLiteral("active"));
    services.setUnitState(QStringLiteral("docker.service"), QStringLiteral("active"));
    QCOMPARE(controller.connectionKey(), QStringLiteral("connected"));

    // 刷新失败之后不能再自称"已连接"（缓存的数据还在，但用户点什么都失败）
    backend.setNextFailure(DockerBackendInterface::Section::Containers,
                           DockerError(DockerError::Kind::DockerUnavailable, QStringLiteral("daemon went away")));
    controller.requestAutomaticRefreshForTesting();
    backend.completeRefresh();
    QVERIFY(controller.updateFailed());
    QCOMPARE(controller.connectionKey(), QStringLiteral("disconnected"));

    // 手动刷新（重新开始）成功后：回到已连接
    controller.refresh();
    backend.completeRefresh();
    QVERIFY(!controller.updateFailed());
    QCOMPARE(controller.connectionKey(), QStringLiteral("connected"));

    // 状态 key 的映射（systemd 的 ActiveState → 稳定 key）
    QCOMPARE(services.stateKeyFor(QStringLiteral("docker.service")), QStringLiteral("running"));
    services.setUnitState(QStringLiteral("docker.service"), QStringLiteral("failed"));
    QCOMPARE(services.stateKeyFor(QStringLiteral("docker.service")), QStringLiteral("failed"));
    services.setUnitState(QStringLiteral("docker.service"), QString());
    QCOMPARE(services.stateKeyFor(QStringLiteral("docker.service")), QStringLiteral("unknown"));
    // 白名单之外的 unit 一律 unknown（不接受任意名字）
    QCOMPARE(services.stateKeyFor(QStringLiteral("sshd.service")), QStringLiteral("unknown"));
}

QTEST_GUILESS_MAIN(StatusControllerTest)

#include "tst_status_controller.moc"
