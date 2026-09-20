/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/docker_backend.h"
#include "domain/image_pull_progress.h"
#include "i18n.h"
#include "model/docker_error_text.h"

#include <QFileInfo>
#include <QtTest>

using namespace Kontainer;

/*!
 * Integration tests against a real Docker Engine (ARCH_V1 §29 / ARCH_V4 §5.3).
 *
 * **GET only** by default; the whole test skips when no usable Docker socket
 * exists locally (CI must not depend on a developer's Docker data directory).
 *
 * The one exception is the explicitly opt-in pull test (`pullsAnImageWhenExplicitlyRequested`):
 * automated tests never touch the user's Docker resources, so it needs
 * `KCM_DOCKER_PULL_TEST=1` and by default pulls an **already present** small
 * image (a repeated pull adds nothing to the registry); override the reference
 * with `KCM_DOCKER_PULL_REFERENCE`.
 */
class DockerBackendIntegrationTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void readsEngineStatus();
    void readsContainerList();
    void readsImageList();
    void missingSocketProducesClearError();
    void pullsAnImageWhenExplicitlyRequested();
    void readsHistoricalLogsOfAnExistingContainer();
    void readsNetworkList();
    void authCheckRequestIsWellFormed();
    void readsVolumeList();
};

void DockerBackendIntegrationTest::initTestCase()
{
    setupTranslationDomain();
    qRegisterMetaType<Kontainer::DockerError>("Kontainer::DockerError");
    qRegisterMetaType<Kontainer::Network>("Kontainer::Network");
    qRegisterMetaType<Kontainer::Volume>("Kontainer::Volume");
    qRegisterMetaType<QList<Kontainer::Volume>>("QList<Kontainer::Volume>");
    qRegisterMetaType<QList<Kontainer::Network>>("QList<Kontainer::Network>");
    qRegisterMetaType<Kontainer::LogLine>("Kontainer::LogLine");
    qRegisterMetaType<QList<Kontainer::LogLine>>("QList<Kontainer::LogLine>");
    qRegisterMetaType<Kontainer::DockerBackendInterface::LogStreamEnd>("Kontainer::DockerBackendInterface::LogStreamEnd");

    const DockerEndpoint endpoint = DockerEndpoint::fromEnvironment();
    if (!endpoint.isValid() || !QFileInfo::exists(endpoint.socketPath())) {
        QSKIP("no Docker socket available on this machine");
    }
}

void DockerBackendIntegrationTest::readsEngineStatus()
{
    DockerBackend backend;
    QSignalSpy engineSpy(&backend, &DockerBackend::engineUpdated);
    QSignalSpy failureSpy(&backend, &DockerBackend::sectionFailed);

    backend.refreshEngine();
    QTRY_VERIFY_WITH_TIMEOUT(engineSpy.count() + failureSpy.count() > 0, 15000);

    QCOMPARE(failureSpy.count(), 0);
    const EngineInfo info = backend.engineInfo();
    QVERIFY(info.available);
    QVERIFY(!info.serverVersion.isEmpty());
    QVERIFY(!info.apiVersion.isEmpty());
    QVERIFY(info.countsAvailable);
    QVERIFY(info.containerTotal >= info.containersRunning);
    QVERIFY(!backend.isLoading());
}

void DockerBackendIntegrationTest::readsContainerList()
{
    DockerBackend backend;
    QSignalSpy containersSpy(&backend, &DockerBackend::containersUpdated);
    QSignalSpy failureSpy(&backend, &DockerBackend::sectionFailed);

    backend.refreshAll();
    QTRY_VERIFY_WITH_TIMEOUT(containersSpy.count() + failureSpy.count() > 0, 15000);

    QCOMPARE(failureSpy.count(), 0);
    const QList<Container> containers = backend.containers();
    for (const Container &container : containers) {
        QVERIFY(!container.id.isEmpty());
        QVERIFY(!container.name.isEmpty());
        QVERIFY(!container.shortId().isEmpty());
        QVERIFY(container.created.isValid());
    }

    // A chunked response must decode to a complete JSON array (parsed entries prove decoding works)
    const EngineInfo info = backend.engineInfo();
    if (info.countsAvailable) {
        QCOMPARE(containers.size(), info.containerTotal);
    }
}

void DockerBackendIntegrationTest::readsImageList()
{
    DockerBackend backend;
    QSignalSpy imagesSpy(&backend, &DockerBackend::imagesUpdated);
    QSignalSpy failureSpy(&backend, &DockerBackend::sectionFailed);

    backend.refreshAll();
    QTRY_VERIFY_WITH_TIMEOUT(imagesSpy.count() + failureSpy.count() > 0, 15000);

    QCOMPARE(failureSpy.count(), 0);
    const QList<Image> images = backend.images();
    for (const Image &image : images) {
        QVERIFY(!image.id.isEmpty());
        QVERIFY(!image.shortId().isEmpty());
        QVERIFY(image.created.isValid());
    }

    const EngineInfo info = backend.engineInfo();
    if (info.countsAvailable) {
        QCOMPARE(images.size(), info.imageCount);
    }
}

void DockerBackendIntegrationTest::missingSocketProducesClearError()
{
    DockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(QStringLiteral("/nonexistent/kontainer-test.sock")));

    QSignalSpy engineSpy(&backend, &DockerBackend::engineUpdated);
    QSignalSpy failureSpy(&backend, &DockerBackend::sectionFailed);

    backend.refreshEngine();
    QTRY_VERIFY_WITH_TIMEOUT(engineSpy.count() + failureSpy.count() > 0, 10000);

    // Every backend failure must be observable (§23.1) and carry understandable user-facing text
    QCOMPARE(failureSpy.count(), 1);
    const auto arguments = failureSpy.first();
    const auto error = arguments.at(1).value<DockerError>();
    QCOMPARE(int(error.kind()), int(DockerError::Kind::DockerUnavailable));
    QVERIFY(!dockerErrorText(error).isEmpty());
    QVERIFY(!backend.engineInfo().available);
}


/*!
 * Pull against a real daemon (ARCH_V4 §2.4 / §5.3, opt-in).
 *
 * Covers the two things hardest to fake in automated tests: what a real engine's
 * chunked progress stream looks like, and how an in-stream error line vs. a clean
 * end is classified by the real implementation.
 *
 * By default it pulls `quay.io/libpod/alpine:latest` (a small public podman image):
 * if it is already local, the pull just reports up to date and adds nothing to the
 * registry. Set `KCM_DOCKER_PULL_REFERENCE` to use another reference.
 *
 * How to run:
 *     KCM_DOCKER_PULL_TEST=1 ./bin/tst_docker_backend_integration pullsAnImageWhenExplicitlyRequested
 */
void DockerBackendIntegrationTest::pullsAnImageWhenExplicitlyRequested()
{
    if (!qEnvironmentVariableIsSet("KCM_DOCKER_PULL_TEST")) {
        QSKIP("opt-in: set KCM_DOCKER_PULL_TEST=1 to allow a real image pull");
    }

    const DockerEndpoint endpoint = DockerEndpoint::fromEnvironment();
    if (!endpoint.isValid() || !QFileInfo::exists(endpoint.socketPath())) {
        QSKIP("no Docker socket available on this machine");
    }

    const QString reference = qEnvironmentVariable("KCM_DOCKER_PULL_REFERENCE", QStringLiteral("quay.io/libpod/alpine:latest"));

    DockerBackend backend;
    backend.setEndpoint(endpoint);

    QList<ImagePullProgress> progress;
    connect(&backend, &DockerBackend::imagePullProgress, this, [&progress](const ImagePullProgress &update) {
        progress.append(update);
    });
    QSignalSpy finishedSpy(&backend, &DockerBackend::mutationFinished);

    backend.pullImage(reference);
    // A first pull may download several MB: allow generous time, but stay bounded
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 180000);

    const auto outcome = finishedSpy.at(0).at(2).value<DockerBackendInterface::MutationOutcome>();
    const DockerError error = finishedSpy.at(0).at(3).value<DockerError>();
    QVERIFY2(outcome == DockerBackendInterface::MutationOutcome::Succeeded,
             qPrintable(QStringLiteral("pull failed: kind=%1 http=%2 detail=%3")
                            .arg(int(error.kind()))
                            .arg(error.httpStatus())
                            .arg(error.detail())));

    QVERIFY2(!progress.isEmpty(), "a real pull must report at least one progress line");
    QVERIFY(!progress.last().reference.isEmpty());
    bool sawComplete = false;
    for (const ImagePullProgress &update : progress) {
        if (update.phase == ImagePullProgress::Phase::Complete) {
            sawComplete = true;
        }
    }
    QVERIFY2(sawComplete, "the pull stream must end with a completed state");
}

/*!
 * Historical logs on a real daemon (ARCH_V5_V8 §3.1.5).
 *
 * **Read-only**: `GET /containers/{id}/logs?follow=0`; create, start and stop nothing.
 * Skipped when the machine has no containers - a test must not use the user's Docker
 * environment as its only fixture (ARCH_V4 §5.3).
 */
void DockerBackendIntegrationTest::readsHistoricalLogsOfAnExistingContainer()
{
    DockerBackend backend;
    QSignalSpy containersSpy(&backend, &DockerBackend::containersUpdated);
    backend.refreshAll();
    QTRY_VERIFY_WITH_TIMEOUT(containersSpy.count() > 0, 15000);

    const QList<Container> containers = backend.containers();
    if (containers.isEmpty()) {
        QSKIP("no container on this machine to read logs from");
    }

    // Pick a container that has output: "read but empty" cannot prove demultiplexing works.
    // Read-only (follow=0), no container is touched; skip when none of them has output.
    //
    // **Only touch test images like alpine / ubuntu**: the user's Docker may hold
    // containers in active use (a Windows compatibility layer, say) which must be left
    // alone even for log reading.
    const auto isTestContainer = [](const Container &container) {
        const QString haystack = (container.name + QLatin1Char(' ') + container.image).toLower();
        return haystack.contains(QLatin1String("alpine")) || haystack.contains(QLatin1String("ubuntu"));
    };

    int inspected = 0;
    for (const Container &container : containers) {
        if (!isTestContainer(container)) {
            continue;
        }
        if (++inspected > 8) {
            break; // don't scan the entire list
        }
        QSignalSpy detailSpy(&backend, &DockerBackend::containerDetailUpdated);
        backend.inspectContainer(container.id);
        QTRY_VERIFY_WITH_TIMEOUT(detailSpy.count() > 0, 15000);
        const bool tty = backend.containerDetail().tty;

        QSignalSpy linesSpy(&backend, &DockerBackend::containerLogLines);
        QSignalSpy finishedSpy(&backend, &DockerBackend::containerLogsFinished);
        backend.startContainerLogs(container.id, tty, false, 20); // follow=0: read the history and finish
        QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() > 0, 20000);

        const auto end = finishedSpy.at(0).at(1).value<DockerBackendInterface::LogStreamEnd>();
        if (end == DockerBackendInterface::LogStreamEnd::Failed) {
            // journald / syslog log drivers are simply unreadable: try the next one
            continue;
        }
        QCOMPARE(end, DockerBackendInterface::LogStreamEnd::Ended);

        int lineCount = 0;
        for (const QVariantList &call : linesSpy) {
            const QList<LogLine> lines = call.at(1).value<QList<LogLine>>();
            for (const LogLine &line : lines) {
                ++lineCount;
                // Demultiplexed lines must be readable: no 8-byte frame-header control bytes left
                QVERIFY2(!line.text.contains(QChar(0x01)) && !line.text.contains(QChar(0x02)),
                         "frame header bytes must never end up in the log text");
                QVERIFY2(!line.text.contains(QChar(0x1b)), "ANSI escapes must be stripped");
            }
        }
        if (lineCount > 0) {
            qInfo("read %d log line(s) from container %s (tty=%d)", lineCount, qPrintable(container.name), int(tty));
            return;
        }
    }

    QSKIP("no alpine/ubuntu test container with readable log output on this machine");
}

/*!
 * Network list on a real daemon (ARCH_V5_V8 §3.2). Read-only `GET /networks`.
 *
 * Asserts that the real payload parses and that the three predefined networks are present
 * (4 networks measured on this machine).
 */
void DockerBackendIntegrationTest::readsNetworkList()
{
    DockerBackend backend;
    QSignalSpy networksSpy(&backend, &DockerBackend::networksUpdated);
    QSignalSpy failureSpy(&backend, &DockerBackend::sectionFailed);

    backend.refreshNetworks();
    QTRY_VERIFY_WITH_TIMEOUT(networksSpy.count() + failureSpy.count() > 0, 15000);
    QCOMPARE(failureSpy.count(), 0);

    const QList<Network> networks = backend.networks();
    QVERIFY2(networks.size() >= 3, "a daemon always has at least bridge/host/none");

    QStringList names;
    for (const Network &network : networks) {
        QVERIFY(!network.id.isEmpty());
        QVERIFY(!network.name.isEmpty());
        QVERIFY(!network.driver.isEmpty());
        names.append(network.name);
    }
    for (const QString &expected : {QStringLiteral("bridge"), QStringLiteral("host"), QStringLiteral("none")}) {
        QVERIFY2(names.contains(expected), qPrintable(QStringLiteral("missing predefined network: ") + expected));
    }

    int predefined = 0;
    for (const Network &network : networks) {
        if (network.isPredefined()) {
            ++predefined;
        }
    }
    QVERIFY2(predefined >= 3, "bridge/host/none must be recognised as pre-defined");
    qInfo("read %d network(s), %d pre-defined", int(networks.size()), predefined);
}

/*!
 * `/auth` request shape (measured correction in ARCH_V5_V8 §2.6).
 *
 * **Read-only, no real credentials**: a clearly nonexistent account is sent to the engine
 * to tell apart
 *   - wrong request **shape** → 400 `invalid X-Registry-Auth ...` (the actual bug:
 *     every registry failed validation)
 *   - right shape, wrong credentials → 401
 *   - right shape, engine cannot reach registry → 500 (timeout wording) = "registry unreachable"
 * So this asserts "**not** a shape error", not "validation succeeded".
 */
void DockerBackendIntegrationTest::authCheckRequestIsWellFormed()
{
    DockerBackend backend;
    QSignalSpy checkedSpy(&backend, &DockerBackend::registryAuthChecked);

    RegistryCredential bogus;
    bogus.serverAddress = QStringLiteral("registry.example.com");
    bogus.username = QStringLiteral("kontainer-not-a-real-account");
    bogus.password = QStringLiteral("definitely-not-the-password");
    backend.checkRegistryAuth(QStringLiteral("registry.example.com"), bogus);
    QTRY_VERIFY_WITH_TIMEOUT(checkedSpy.count() > 0, 30000);

    const auto result = checkedSpy.at(0).at(1).value<DockerBackendInterface::AuthCheckResult>();
    const QString detail = checkedSpy.at(0).at(2).toString();
    QVERIFY2(!detail.contains(QLatin1String("invalid X-Registry-Auth")),
             qPrintable(QStringLiteral("the credential payload must be accepted by the engine: ") + detail));
    QVERIFY2(result != DockerBackendInterface::AuthCheckResult::Failed,
             qPrintable(QStringLiteral("a well-formed request must not classify as a generic failure: ") + detail));
    qInfo("auth check result=%d (1=invalid credentials, 2=unreachable)", int(result));
}

/*!
 * Volume list on a real daemon (ARCH_V5_V8 §3.5). Read-only `GET /volumes`.
 *
 * This machine may have no volume at all (it has none), so the assertion is that the
 * payload shape parses, not that volumes exist.
 */
void DockerBackendIntegrationTest::readsVolumeList()
{
    DockerBackend backend;
    QSignalSpy volumesSpy(&backend, &DockerBackend::volumesUpdated);
    QSignalSpy failureSpy(&backend, &DockerBackend::sectionFailed);

    backend.refreshVolumes(true);
    QTRY_VERIFY_WITH_TIMEOUT(volumesSpy.count() + failureSpy.count() > 0, 20000);
    QCOMPARE(failureSpy.count(), 0);

    const QList<Volume> volumes = backend.volumes();
    for (const Volume &volume : volumes) {
        QVERIFY(!volume.name.isEmpty());
        QVERIFY(!volume.driver.isEmpty());
    }
    qInfo("read %d volume(s)", int(volumes.size()));
}

QTEST_GUILESS_MAIN(DockerBackendIntegrationTest)

#include "tst_docker_backend_integration.moc"
