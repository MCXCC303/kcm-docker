/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
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
 * Write operation orchestration (ARCH_V4 §2.2.4 / §5.1).
 *
 * Covers the "upper-layer rules": admission, serialisation, the result channel, read-after-write,
 * privilege degradation. Request shapes are the backend contract tests' job
 * (tst_docker_backend_against_fake_engine).
 */
class OperationControllerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();

    /* permission gate */
    void writableEndpointAllowsOperations();
    void nonWritableEndpointHidesOperations();
    void enginePermissionDenialDegradesSessionToReadOnly();

    /* serialisation and busy state */
    void sameTargetIsSerialised();
    void differentTargetsRunInParallel();

    /* result channel */
    void successRefreshesAffectedDataSets();
    void unchangedIsReportedAsSuccess();
    void failureCarriesCategoryAndEngineDetail();
    void removalEmitsNavigationSignal();
    void dismissClearsResult();

    /* pulls */
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

    // The UI hides write entries; even so a call must be refused without a backend request
    m_operations->startContainer(QStringLiteral("abc"));
    QCOMPARE(m_backend->mutationCount(Mutation::StartContainer), 0);
    QCOMPARE(m_operations->resultKey(), QStringLiteral("error"));
}

void OperationControllerTest::enginePermissionDenialDegradesSessionToReadOnly()
{
    QVERIFY(m_operations->writeAllowed());

    m_operations->startContainer(QStringLiteral("abc"));
    m_backend->completeMutations(MutationOutcome::Failed, DockerError(DockerError::Kind::PermissionDenied, QStringLiteral("access denied"), 403));

    // Degradation is final for this session: no write entries, no recovery
    QVERIFY(!m_operations->writeAllowed());
    QCOMPARE(m_operations->resultKey(), QStringLiteral("error"));
    QCOMPARE(m_operations->resultCategoryKey(), QStringLiteral("userActionable"));
    QVERIFY(!m_operations->writeAccessText().isEmpty());

    // refreshWriteAccess() must not undo it (the socket is writable, after all)
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

    // Same target again: refused, no second request
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
    // Read-after-write: container list and storage usage must refresh immediately
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
    // The engine text must be reachable: the text is a summary, detail holds the "why"
    QVERIFY(m_operations->resultDetailText().contains(QStringLiteral("running container")));
    // 409 is not a permission problem: one conflict must not force read-only
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
 * The pull list (implementing ARCH_V4 §2.4): pulls are long tasks that continue in the background,
 * several different images pull at once, and a failure reason stays in the list instead of being dropped.
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
    // The user sees the normalised reference (missing tag → latest)
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
    // A succeeded record stays in the list (marked done) instead of vanishing
    QCOMPARE(m_operations->pulls()->count(), 1);
    QCOMPARE(m_operations->pulls()->index(0, 0).data(ImagePullModel::StatusKeyRole).toString(), QStringLiteral("succeeded"));
    QCOMPARE(m_operations->resultKey(), QStringLiteral("success"));
    QVERIFY(!listSpy.isEmpty());
}

void OperationControllerTest::differentImagesPullConcurrently()
{
    m_operations->pullImage(QStringLiteral("alpine"));
    m_operations->pullImage(QStringLiteral("busybox:latest"));

    // Two concurrent pulls, independent of each other (the whole point of this change)
    QCOMPARE(m_backend->mutationCount(Mutation::PullImage), 2);
    QCOMPARE(m_operations->activePullCount(), 2);
    QCOMPARE(m_operations->pulls()->count(), 2);

    // Pulling the same reference again: refused, no second request
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
    // A pull can take long and must not put the page in "busy" (the refresh button would keep spinning)
    QVERIFY(!m_operations->busy());

    m_operations->startContainer(QStringLiteral("cid-1"));
    QCOMPARE(m_backend->mutationCount(Mutation::StartContainer), 1);
}

/*!
 * Private registries: a pull hands the matching wallet credential to the backend (ARCH_V5_V8 §2.6).
 *
 * Pinned here: the credential really reaches the pull path, and a missing one stays anonymous — a 401
 * comes from the engine and the UI shows the reason; an unavailable wallet does not break pulling.
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

    // Registry without a credential: anonymous pull (empty, not the previous one's leftovers)
    m_operations->pullImage(QStringLiteral("alpine:3.19"));
    QVERIFY2(m_backend->lastPullCredential().isEmpty(), "an unauthenticated registry must pull anonymously");

    // Wallet unavailable (not open): also degrades to anonymous instead of refusing the pull
    CredentialStore closedWallet(&wallet);
    m_operations->setCredentialStore(&closedWallet);
    m_operations->pullImage(QStringLiteral("ghcr.io/team/other:2.0"));
    QVERIFY(m_backend->lastPullCredential().isEmpty());
    QCOMPARE(m_backend->lastPullCredential().serverAddress, QString());
}

/*! Create network (ARCH_V5_V8 §3.3): validated in C++, a failure gives a stable key and no request. */
void OperationControllerTest::createNetworkValidatesInput()
{
    QList<Network> existing;
    Network bridge;
    bridge.id = QString(64, QLatin1Char('b'));
    bridge.name = QStringLiteral("bridge");
    bridge.driver = QStringLiteral("bridge");
    existing.append(bridge);
    m_backend->setNetworks(existing);

    // Name: empty, spaces and other illegal characters must be rejected
    QVERIFY(!m_operations->createNetwork(QString()));
    QCOMPARE(m_operations->resultDetailText(), QStringLiteral("nameRequired"));
    QVERIFY(!m_operations->createNetwork(QStringLiteral("my net")));
    QCOMPARE(m_operations->resultDetailText(), QStringLiteral("nameInvalid"));

    // Subnet / gateway format
    QVERIFY(!m_operations->createNetwork(QStringLiteral("app_net"), QStringLiteral("not-a-cidr")));
    QCOMPARE(m_operations->resultDetailText(), QStringLiteral("subnetInvalid"));
    QVERIFY(!m_operations->createNetwork(QStringLiteral("app_net"), QString(), QStringLiteral("172.30.0.1")));
    QCOMPARE(m_operations->resultDetailText(), QStringLiteral("gatewayNeedsSubnet"));
    QVERIFY(!m_operations->createNetwork(QStringLiteral("app_net"), QStringLiteral("172.30.0.0/16"), QStringLiteral("not-an-ip")));
    QCOMPARE(m_operations->resultDetailText(), QStringLiteral("gatewayInvalid"));

    // Duplicate of an existing network (case-insensitive, as the daemon judges it)
    QVERIFY(!m_operations->createNetwork(QStringLiteral("Bridge")));
    QCOMPARE(m_operations->resultDetailText(), QStringLiteral("nameInUse"));

    // A validation failure must send no request at all
    QCOMPARE(m_backend->mutationCalls().size(), 0);

    // Valid input: the request goes out with the fields passed through
    QVERIFY(m_operations->createNetwork(QStringLiteral(" app_net "),
                                        QStringLiteral("172.30.0.0/16"),
                                        QStringLiteral("172.30.0.1"),
                                        true,
                                        true,
                                        {QVariantMap {{QStringLiteral("key"), QStringLiteral("owner")},
                                                      {QStringLiteral("value"), QStringLiteral("team-a")}}}));
    QCOMPARE(m_backend->lastNetworkCreate().name, QStringLiteral("app_net")); // trimmed
    QCOMPARE(m_backend->lastNetworkCreate().driver, QStringLiteral("bridge")); // bridges only this round
    QCOMPARE(m_backend->lastNetworkCreate().subnet, QStringLiteral("172.30.0.0/16"));
    QVERIFY(m_backend->lastNetworkCreate().internal);
    QVERIFY(m_backend->lastNetworkCreate().attachable);
    QCOMPARE(m_backend->lastNetworkCreate().labels.size(), 1);
    QCOMPARE(m_backend->lastNetworkCreate().labels.first().first, QStringLiteral("owner"));
}

/*!
 * Create/remove network share one result channel and trigger read-after-write on success.
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

    // Engine refuses (e.g. overlapping subnet): its text lands in the result, no local state changes
    QVERIFY(m_operations->createNetwork(QStringLiteral("other_net")));
    m_backend->completeMutations(DockerBackendInterface::MutationOutcome::Failed,
                                 DockerError(DockerError::Kind::EngineError, QStringLiteral("Pool overlaps with other one")));
    // The engine text goes to the "technical detail" field, so the user can read the actual cause
    QVERIFY2(m_operations->resultDetailText().contains(QStringLiteral("Pool overlaps")),
             qPrintable(m_operations->resultDetailText()));
    QVERIFY(!m_operations->resultText().isEmpty());
}

/*!
 * Remove network: write gate + target busy tracking (predefined networks are blocked by UI and daemon).
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

    // Read-only: no request, an explicit result (the UI hides the entry; this is the fallback)
    m_backend->setEndpoint(DockerEndpoint::unixSocket(QStringLiteral("/tmp/does-not-exist.sock")));
    m_operations->refreshWriteAccess();
    QVERIFY(!m_operations->writeAllowed());
    const int callsBefore = m_backend->mutationCalls().size();
    m_operations->removeNetwork(id, QStringLiteral("app_net"));
    QCOMPARE(m_backend->mutationCalls().size(), callsBefore);
    QVERIFY(!m_operations->resultText().isEmpty());
}

/*!
 * Network connect/disconnect (ARCH_V5_V8 §3.4): argument passing, alias parsing, read-after-write.
 */
void OperationControllerTest::connectAndDisconnectContainerToNetwork()
{
    const QString networkId = QString(64, QLatin1Char('n'));
    const QString containerId = QString(64, QLatin1Char('c'));
    QSignalSpy networksSpy(m_operations, &OperationController::networksChanged);
    QSignalSpy containerSpy(m_operations, &OperationController::containerStateChanged);

    // Aliases are comma-separated input: whitespace trimmed, empty entries dropped
    QVERIFY(m_operations->connectContainerToNetwork(networkId, containerId, QStringLiteral(" app , api ,, ")));
    QCOMPARE(m_backend->lastNetworkConnect().first, networkId);
    QCOMPARE(m_backend->lastNetworkConnect().second, containerId);
    QCOMPARE(m_backend->lastNetworkConnectAliases(), QStringList({QStringLiteral("app"), QStringLiteral("api")}));

    m_backend->completeMutations(DockerBackendInterface::MutationOutcome::Succeeded);
    QCOMPARE(networksSpy.count(), 1);
    QCOMPARE(containerSpy.count(), 1);
    QCOMPARE(containerSpy.at(0).at(0).toString(), containerId);
    QVERIFY2(m_operations->resultText().contains(QStringLiteral("connected")), qPrintable(m_operations->resultText()));

    // Disconnect: force off (the UI offers no "force" option)
    QVERIFY(m_operations->disconnectContainerFromNetwork(networkId, containerId));
    QCOMPARE(m_backend->lastNetworkDisconnect().first, networkId);
    QCOMPARE(m_backend->lastNetworkDisconnect().second, containerId);
    m_backend->completeMutations(DockerBackendInterface::MutationOutcome::Succeeded);
    QCOMPARE(networksSpy.count(), 2);

    // Missing arguments: no request, an explicit result
    const int callsBefore = m_backend->mutationCalls().size();
    QVERIFY(!m_operations->connectContainerToNetwork(QString(), containerId));
    QVERIFY(!m_operations->disconnectContainerFromNetwork(networkId, QString()));
    QCOMPARE(m_backend->mutationCalls().size(), callsBefore);
    QCOMPARE(m_operations->resultDetailText(), QStringLiteral("missingTarget"));
}

/*! Create volume (ARCH_V5_V8 §3.5): validation in C++, a failure sends no request. */
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
    QCOMPARE(m_backend->mutationCalls().size(), 0); // a failed validation sends no request

    QSignalSpy volumesSpy(m_operations, &OperationController::volumesChanged);
    QVERIFY(m_operations->createVolume(QStringLiteral(" cache "), QStringLiteral("local"),
                                       {QVariantMap {{QStringLiteral("key"), QStringLiteral("owner")},
                                                     {QStringLiteral("value"), QStringLiteral("team-a")}}}));
    QCOMPARE(m_backend->lastCreatedVolumeName(), QStringLiteral("cache")); // trimmed
    QCOMPARE(m_backend->lastCreatedVolumeDriver(), QStringLiteral("local"));

    m_backend->completeMutations(DockerBackendInterface::MutationOutcome::Succeeded);
    QCOMPARE(volumesSpy.count(), 1);
    QVERIFY2(m_operations->resultText().contains(QStringLiteral("cache")), qPrintable(m_operations->resultText()));
    // Read-after-write: both the list and storage usage are re-read
    QVERIFY(m_backend->refreshCount(DockerBackendInterface::Section::Volumes) >= 1);
    QVERIFY(m_backend->refreshCount(DockerBackendInterface::Section::Storage) >= 1);
}

/*!
 * Remove and prune (§3.5): remove has **no force**; prune's detail arrives via a signal.
 */
void OperationControllerTest::removeAndPruneVolumes()
{
    const QString name = QStringLiteral("cache");
    QSignalSpy volumesSpy(m_operations, &OperationController::volumesChanged);

    QVERIFY(m_operations->removeVolume(name));
    QCOMPARE(m_backend->lastRemovedVolume(), name);
    m_backend->completeMutation(OperationTarget::volume(name), DockerBackendInterface::MutationOutcome::Succeeded);
    QCOMPARE(volumesSpy.count(), 1);

    // Engine refuses while a container uses it: the text lands in the result (UI says "detach it first")
    QVERIFY(m_operations->removeVolume(QStringLiteral("app_data")));
    m_backend->completeMutation(OperationTarget::volume(QStringLiteral("app_data")),
                                DockerBackendInterface::MutationOutcome::Failed,
                                DockerError(DockerError::Kind::Conflict, QStringLiteral("volume is in use")));
    QVERIFY2(m_operations->resultDetailText().contains(QStringLiteral("in use")),
             qPrintable(m_operations->resultDetailText()));

    // Prune: request first, then the signal detail completes "how much was reclaimed"
    QVERIFY(m_operations->pruneVolumes());
    QCOMPARE(m_backend->pruneCallCount(), 1);
    m_backend->completePrune({QStringLiteral("cache"), QStringLiteral("legacy")}, 2048);
    m_backend->completeMutation(OperationTarget::volumePrune(), DockerBackendInterface::MutationOutcome::Succeeded);
    // Only successful removals and prunes trigger a re-read (the failed one must not)
    QCOMPARE(volumesSpy.count(), 2);
    QVERIFY2(m_operations->resultText().contains(QStringLiteral("2")), qPrintable(m_operations->resultText()));
    QVERIFY2(!m_operations->resultText().isEmpty(), qPrintable(m_operations->resultText()));
    QVERIFY2(m_operations->resultDetailText().contains(QStringLiteral("cache")),
             qPrintable(m_operations->resultDetailText())); // the detail lists the deleted volume names

    // Nothing to prune: say so, do not fake success (detail signal arrives before mutationFinished)
    QVERIFY(m_operations->pruneVolumes());
    m_backend->completePrune({}, 0);
    m_backend->completeMutation(OperationTarget::volumePrune(), DockerBackendInterface::MutationOutcome::Succeeded);
    QVERIFY2(m_operations->resultText().contains(QStringLiteral("Nothing")), qPrintable(m_operations->resultText()));
    QVERIFY(m_operations->resultDetailText().isEmpty());
}

/*!
 * Create container validation (ARCH_V5_V8 §4.3/§4.6): checks that need backend data are in C++ too,
 * a failure gives a stable key.
 */
/*!
 * Pause / resume (user-reported issue ①): the request hits the right endpoint and the result text
 * says what happened.
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

    // Read-only: send nothing and say plainly that it is read-only
    // (a separate backend pointed at a missing socket makes the write gate deny)
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
    // Port field order is {ip, container port, host port, protocol}
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

    // Name rules
    QVariantMap request = baseRequest();
    request.insert(QStringLiteral("name"), QStringLiteral("has space"));
    QVERIFY(!m_operations->createContainer(request));
    QCOMPARE(m_operations->resultDetailText(), QStringLiteral("nameInvalid"));

    // Duplicate of an existing container (case-insensitive)
    request = baseRequest();
    request.insert(QStringLiteral("name"), QStringLiteral("WEB"));
    QVERIFY(!m_operations->createContainer(request));
    QCOMPARE(m_operations->resultDetailText(), QStringLiteral("nameInUse"));

    // Image not local: refused by default, allowed with `allowMissingImage`
    request = baseRequest();
    request.insert(QStringLiteral("image"), QStringLiteral("busybox:latest"));
    QVERIFY(!m_operations->createContainer(request));
    QCOMPARE(m_operations->resultDetailText(), QStringLiteral("imageNotLocal"));
    QVERIFY2(m_operations->createContainer(request, /*allowMissingImage=*/true),
             "the UI can offer 'pull first' and still submit");
    // Finish that submit, else the target stays "in flight" and later same-name submits are refused
    m_backend->completeMutation(OperationTarget::container(QStringLiteral("worker")),
                                DockerBackendInterface::MutationOutcome::Succeeded);
    // Baseline: no write in flight (completeMutation drops finished calls from the list)
    const int callsAfterFirstSubmit = m_backend->mutationCalls().size();

    // Host port conflict (0.0.0.0 and a specific address also conflict)
    request = baseRequest();
    request.insert(QStringLiteral("ports"),
                   QVariantList {QVariantMap {{QStringLiteral("hostIp"), QStringLiteral("127.0.0.1")},
                                              {QStringLiteral("hostPort"), 8080},
                                              {QStringLiteral("containerPort"), 80},
                                              {QStringLiteral("protocol"), QStringLiteral("tcp")}}});
    QVERIFY(!m_operations->createContainer(request, true));
    QCOMPARE(m_operations->resultDetailText(), QStringLiteral("portInUse"));

    // The mount destination must be an absolute path
    request = baseRequest();
    request.insert(QStringLiteral("mounts"),
                   QVariantList {QVariantMap {{QStringLiteral("type"), QStringLiteral("bind")},
                                              {QStringLiteral("source"), QStringLiteral("/srv/x")},
                                              {QStringLiteral("destination"), QStringLiteral("relative")}}});
    QVERIFY(!m_operations->createContainer(request, true));
    QCOMPARE(m_operations->resultDetailText(), QStringLiteral("pathNotAbsolute"));

    // These validation failures also sent no request
    QCOMPARE(m_backend->mutationCalls().size(), callsAfterFirstSubmit);

    // Valid request: fields pass through as given
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
 * "Create and start": two serial steps, and success or failure must name which step (§4.6).
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

    // Step 1: the engine returned an id
    m_backend->completeContainerCreate(QStringLiteral("new-id"));
    QCOMPARE(createdSpy.count(), 1);
    QCOMPARE(createdSpy.at(0).at(0).toString(), QStringLiteral("new-id"));
    QCOMPARE(createdSpy.at(0).at(1).toBool(), false); // not started yet
    m_backend->completeMutation(OperationTarget::container(QStringLiteral("web")),
                                DockerBackendInterface::MutationOutcome::Succeeded);
    QVERIFY2(m_operations->resultText().contains(QStringLiteral("created")), qPrintable(m_operations->resultText()));

    // Step 2 succeeds → a fresh "created and started" result with started=true for the UI
    m_backend->completeMutation(OperationTarget::container(QStringLiteral("new-id")),
                                DockerBackendInterface::MutationOutcome::Succeeded);
    QCOMPARE(createdSpy.count(), 2);
    QCOMPARE(createdSpy.at(1).at(1).toBool(), true);
    QVERIFY2(m_operations->resultText().contains(QStringLiteral("started")), qPrintable(m_operations->resultText()));

    // Start fails: the text must say "the container was created but starting failed"
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

    // Only the selected pull is cancelled
    QCOMPARE(m_backend->cancelledPulls(), QStringList {QStringLiteral("busybox:latest")});

    m_backend->completeMutations(MutationOutcome::Cancelled);
    QCOMPARE(m_operations->activePullCount(), 0);
    QCOMPARE(m_operations->resultKey(), QStringLiteral("cancelled"));
    // Cancelling is not an error: no error styling, no effect on write access
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

    // A failure **stays in the list** with its reason: open and close the pull window and it survives
    QCOMPARE(m_operations->pulls()->count(), 1);
    const QModelIndex index = m_operations->pulls()->index(0, 0);
    QCOMPARE(index.data(ImagePullModel::StatusKeyRole).toString(), QStringLiteral("failed"));
    QCOMPARE(index.data(ImagePullModel::DetailTextRole).toString(), QStringLiteral("manifest unknown"));
    QCOMPARE(m_operations->resultKey(), QStringLiteral("error"));

    // The user can dismiss it afterwards; the list is then empty
    m_operations->dismissPull(QStringLiteral("nope/nope:none"));
    QCOMPARE(m_operations->pulls()->count(), 0);
    QCOMPARE(m_operations->activePullCount(), 0);
}

void OperationControllerTest::clearFinishedPullsKeepsActiveOnes()
{
    m_operations->pullImage(QStringLiteral("alpine"));
    m_operations->pullImage(QStringLiteral("busybox"));
    // First pull finishes, the second continues
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
 * Host port conflict detection (reported in real use: creation let it pass, the failure only came at start
 * with `Bind for 0.0.0.0:8100 failed: port is already allocated`).
 *
 * Two points:
 *  - **only containers actually holding the port** conflict: an exited container holds none, so counting
 *    it would false-positive;
 *  - a conflict must name **who holds it**, so the UI can tell the user which container to stop.
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

    // A running container holds it → conflict, and the name is reported
    QVERIFY(m_operations->hostPortInUse(QStringLiteral("0.0.0.0"), 8100));
    QCOMPARE(m_operations->hostPortHolder(QStringLiteral("0.0.0.0"), 8100), QStringLiteral("holder"));
    // A specific address and a wildcard conflict (same port)
    QVERIFY(m_operations->hostPortInUse(QStringLiteral("::"), 8100));
    QVERIFY(m_operations->hostPortInUse(QStringLiteral("127.0.0.1"), 8100));

    // Exited containers hold no port → no conflict (not running means not bound; blocking hits other apps)
    QVERIFY2(!m_operations->hostPortInUse(QStringLiteral("0.0.0.0"), 8200),
             "a stopped container does not hold its published ports");
    QVERIFY(m_operations->hostPortHolder(QStringLiteral("0.0.0.0"), 8200).isEmpty());

    // Different specific addresses do not conflict
    QVERIFY(!m_operations->hostPortInUse(QStringLiteral("192.168.1.5"), 8300));
    QVERIFY(m_operations->hostPortInUse(QStringLiteral("127.0.0.1"), 8300));

    // A random port (0) never conflicts
    QVERIFY(!m_operations->hostPortInUse(QStringLiteral("0.0.0.0"), 0));
}

/*!
 * A port conflict that only fails at start: the text must say "use another port or stop that container"
 * and pull the port number out of the engine text (users cannot read
 * `driver failed programming external connectivity`).
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
    // Do not hand the raw engine text to the user (that is the "technical detail" slot)
    QVERIFY2(!text.contains(QStringLiteral("driver failed")), qPrintable(text));
}


QTEST_MAIN(OperationControllerTest)

#include "tst_operation_controller.moc"
