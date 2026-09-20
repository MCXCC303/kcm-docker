/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "i18n.h"
#include "model/docker_error_text.h"
#include "refresh_policy.h"
#include "model/status_controller.h"
#include "support/mock_docker_backend.h"

#include <QtTest>

using namespace Kontainer;

/*! UI state machine and error isolation (ARCH_V1 §14/§15/§16, mock backend §30). */
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

    // Containers still work → the whole page must not go unusable (§15)
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

    // The next success must clear the error
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

    // ARCH_V2 §13.1: default is 5 s; intervals live in RefreshPolicy so tests use no magic numbers
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
 * §14: QML must not assemble state from booleans, so every data set needs one enum state.
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

    // Before the first refresh: Engine is Loading, not "disconnected"
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
    QCOMPARE(int(controller.imagesState()), int(StatusController::ListState::Empty)); // ok but list empty
    // QML contract: the state keys must stay stable (QML switches on these strings)
    QCOMPARE(controller.stateKey(), QStringLiteral("ready"));
    QCOMPARE(controller.engineStateKey(), QStringLiteral("ready"));
    QCOMPARE(controller.containersStateKey(), QStringLiteral("ready"));
    QCOMPARE(controller.imagesStateKey(), QStringLiteral("empty"));

    // Refresh again with data present → refreshing (UI shows Connected + spinner)
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
    backend.setContainers({}); // an empty list is not an error (§38)

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
    info.available = true; // /_ping and /version succeed
    info.countsAvailable = false; // but /info fails
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
 * §15: Last Updated must reflect the real last success, not the last attempt.
 */
void StatusControllerTest::tracksLastSuccessfulUpdate()
{
    MockDockerBackend backend;
    StatusController controller(&backend);

    QVERIFY(!controller.lastUpdated().isValid());
    QVERIFY(!controller.updateFailed());
    QVERIFY(!controller.stale());

    controller.refresh();
    QVERIFY(!controller.lastUpdated().isValid()); // no success yet

    backend.completeRefresh();
    QVERIFY(controller.lastUpdated().isValid());
    QVERIFY(!controller.updateFailed());

    /*
     * Consecutive failures reach the threshold → stale (§16).
     *
     * This must use the **automatic** path: a manual refresh resets the failure count (fix B2 — after
     * recovery it must not still say "update failed"), so refresh() never accumulates to stale, which
     * is itself the assertion for that fix (see manualRefreshClearsTheStickyFailureFlag).
     */
    const DockerError failure(DockerError::Kind::EngineError, QStringLiteral("boom"));
    for (int i = 0; i < RefreshPolicy::kStaleAfterFailedCycles; ++i) {
        backend.setNextFailure(DockerBackendInterface::Section::Containers, failure);
        controller.requestAutomaticRefreshForTesting();
        backend.completeRefresh();
    }
    QVERIFY(controller.updateFailed());
    QVERIFY(controller.stale());
    QVERIFY(controller.lastUpdated().isValid()); // the old success time is kept
}

/*!
 * §30/§55: the retry entry point after a storage failure retries that data set only, and QML has no
 * direct access to the backend (§4/§43), so it goes through an explicit controller method.
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
 * B3: after a first attempt that failed, the engine state must be "unavailable", not stuck at "loading".
 */
void StatusControllerTest::failedRefreshDoesNotStayInLoading()
{
    MockDockerBackend backend;
    const DockerError error(DockerError::Kind::DockerUnavailable, QStringLiteral("Cannot connect to the Docker daemon"));
    backend.setNextFailure(DockerBackendInterface::Section::Engine, error);
    backend.setNextFailure(DockerBackendInterface::Section::Containers, error);
    backend.setNextFailure(DockerBackendInterface::Section::Images, error);

    StatusController controller(&backend);
    QCOMPARE(controller.engineStateKey(), QStringLiteral("loading")); // never requested: loading is right

    controller.refresh();
    backend.completeRefresh();

    QVERIFY2(!controller.busy(), "a finished (failed) refresh must not keep the busy flag");
    QCOMPARE(controller.engineStateKey(), QStringLiteral("unavailable"));
    QVERIFY2(!controller.engineError().isEmpty(), "the reason must reach the UI");
}

/*!
 * B2: a manual refresh is a fresh start — the previous failure flag must not stick.
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

    // After recovery the user hits refresh: the flag clears first, then this cycle's result decides
    controller.refresh();
    QVERIFY2(!controller.updateFailed(), "a manual refresh restarts the failure accounting");
    backend.completeRefresh();
}

/*!
 * B4: when a request hangs forever the watchdog must abandon it and return the UI to a retryable state.
 */
void StatusControllerTest::watchdogAbandonsStuckRequests()
{
    MockDockerBackend backend;
    StatusController controller(&backend);
    // The watchdog is lowered to 50 ms here (default is 20 s, too slow for a test)
    controller.setInFlightWatchdogMs(50);
    QCOMPARE(controller.inFlightWatchdogMs(), 50);

    backend.setStallRequests(true); // half-dead daemon: socket accepted but no data comes back
    controller.refresh();
    QVERIFY2(controller.busy(), "the refresh is in flight");

    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 3000);
    QCOMPARE(controller.engineStateKey(), QStringLiteral("unavailable"));
    QVERIFY2(!controller.engineError().isEmpty(), "the timeout must be reported as a reason");

    // After recovery a new refresh works (an abandoned one does not wedge the controller)
    backend.setStallRequests(false);
    controller.refresh();
    backend.completeRefresh();
    QVERIFY2(!controller.busy(), "a later refresh must work again");
}

/*!
 * B1: service state decides the "connected" wording — a live socket with a stopped service
 * must not be reported as plain "connected".
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

    // All three units running + refresh succeeded → connected
    controller.refresh();
    backend.completeRefresh();
    QCOMPARE(controller.connectionKey(), QStringLiteral("connected"));

    // docker.service stopped with the socket up (the user-reported case) → must say "service not running"
    services.setUnitState(QStringLiteral("docker.service"), QStringLiteral("inactive"));
    QCOMPARE(controller.connectionKey(), QStringLiteral("connectedServicesDown"));

    // All services stopped: even with cached engine data, it must still say "service not running"
    services.setUnitState(QStringLiteral("docker.socket"), QStringLiteral("inactive"));
    QCOMPARE(controller.connectionKey(), QStringLiteral("connectedServicesDown"));

    /*
     * Never connected (no engine data) + services down → "disconnected, services down".
     *
     * The controller **keeps** the last engine data (error isolation), so this needs a fresh controller;
     * clearing the old one's engine data cannot happen and should not.
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

    // Services recover: with cached engine data, "connected" is honest (a refresh is in flight)
    services.setUnitState(QStringLiteral("docker.socket"), QStringLiteral("active"));
    services.setUnitState(QStringLiteral("docker.service"), QStringLiteral("active"));
    QCOMPARE(controller.connectionKey(), QStringLiteral("connected"));

    // After a failed refresh it must not claim "connected" (cache is stale, every action fails)
    backend.setNextFailure(DockerBackendInterface::Section::Containers,
                           DockerError(DockerError::Kind::DockerUnavailable, QStringLiteral("daemon went away")));
    controller.requestAutomaticRefreshForTesting();
    backend.completeRefresh();
    QVERIFY(controller.updateFailed());
    QCOMPARE(controller.connectionKey(), QStringLiteral("disconnected"));

    // After a successful manual refresh: back to connected
    controller.refresh();
    backend.completeRefresh();
    QVERIFY(!controller.updateFailed());
    QCOMPARE(controller.connectionKey(), QStringLiteral("connected"));

    // State key mapping (systemd ActiveState → stable key)
    QCOMPARE(services.stateKeyFor(QStringLiteral("docker.service")), QStringLiteral("running"));
    services.setUnitState(QStringLiteral("docker.service"), QStringLiteral("failed"));
    QCOMPARE(services.stateKeyFor(QStringLiteral("docker.service")), QStringLiteral("failed"));
    services.setUnitState(QStringLiteral("docker.service"), QString());
    QCOMPARE(services.stateKeyFor(QStringLiteral("docker.service")), QStringLiteral("unknown"));
    // Units outside the allow-list are always unknown (no arbitrary names)
    QCOMPARE(services.stateKeyFor(QStringLiteral("sshd.service")), QStringLiteral("unknown"));
}

QTEST_GUILESS_MAIN(StatusControllerTest)

#include "tst_status_controller.moc"
