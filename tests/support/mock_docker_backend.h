/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/docker_backend_interface.h"
#include "backend/service_status.h"

#include <QHash>
#include <QSet>

namespace Kontainer
{

/*!
 * Controllable service status source (for tests): all three units run by default, tests set units.
 */
class FakeServiceStatus : public ServiceStatusBackend
{
    Q_OBJECT

public:
    explicit FakeServiceStatus(QObject *parent = nullptr)
        : ServiceStatusBackend(parent)
    {
        for (const QString &unit : managedServiceUnits()) {
            ServiceState state;
            state.unit = unit;
            state.activeState = QStringLiteral("active");
            state.unitFileState = QStringLiteral("enabled");
            state.subState = QStringLiteral("running");
            m_services.append(state);
        }
    }

    void query() override
    {
        ++queryCount;
        Q_EMIT servicesChanged();
    }

    QList<ServiceState> services() const override
    {
        return m_services;
    }

    /*! Set a unit's state (`activeState` empty = systemd cannot find it). */
    void setUnitState(const QString &unit, const QString &activeState, const QString &unitFileState = QStringLiteral("enabled"))
    {
        for (ServiceState &state : m_services) {
            if (state.unit == unit) {
                state.activeState = activeState;
                state.unitFileState = unitFileState;
                state.subState = activeState == QLatin1String("active") ? QStringLiteral("running") : QStringLiteral("dead");
            }
        }
        Q_EMIT servicesChanged();
    }

    int queryCount = 0;

private:
    QList<ServiceState> m_services;
};

/*!
 * Backend for tests (ARCH_V1 §30).
 *
 * Keeps model / controller tests independent of a real Docker daemon:
 *  - the test sets the data "to be read"
 *  - refresh*() only records the request and enters loading; the test decides when it finishes by
 *    calling completeRefresh()
 *  - a failure can be injected per dataset, to verify error isolation
 */
class MockDockerBackend : public DockerBackendInterface
{
    Q_OBJECT

public:
    explicit MockDockerBackend(QObject *parent = nullptr);

    void setEngineInfo(const EngineInfo &info);
    void setContainers(const QList<Container> &containers);
    void setImages(const QList<Image> &images);
    void setNetworks(const QList<Network> &networks);
    void setVolumes(const QList<Volume> &volumes);
    /*! Whether the last refresh asked for usage stats (the UI can skip it when it only needs names). */
    bool lastVolumesRefreshUsedUsage() const
    {
        return m_lastVolumesIncludeUsage;
    }
    /*! Last network-create request (asserts validation and request content). */
    NetworkCreateRequest lastNetworkCreate() const
    {
        return m_lastNetworkCreate;
    }
    /*! Id of the last removed network. */
    QString lastRemovedNetwork() const
    {
        return m_lastRemovedNetwork;
    }
    void setStorageUsage(const StorageUsage &usage);
    void setContainerDetail(const ContainerDetail &detail);
    /*! Store a detail per id (the render tool prepares both the running and the paused page). */
    void setContainerDetailForId(const QString &id, const ContainerDetail &detail);
    void setImageDetail(const ImageDetail &detail);
    void setContainerStats(const ContainerStats &stats);
    void setEndpointName(const QString &name);
    /*! The capability gate (DockerCapabilities) reads this endpoint; tests build writable/read-only cases. */
    void setEndpoint(const DockerEndpoint &endpoint);

    /*! Make the next refresh of the given dataset fail. */
    void setNextFailure(Section section, const DockerError &error);
    void clearFailures();

    /* --- Write operations (ARCH_V4 §2.2.4) --- */
    struct MutationCall {
        Mutation mutation = Mutation::StartContainer;
        QString targetKey;
        /*! force flag when removing an image. */
        bool force = false;
    };
    /*! Mutations issued but not yet finished. */
    QList<MutationCall> mutationCalls() const
    {
        return m_mutationCalls;
    }
    int mutationCount(Mutation mutation) const;
    QString lastMutationTarget(Mutation mutation) const;
    /*! References requested for cancellation (cancel by reference, ARCH_V4 §2.4). */
    QStringList cancelledPulls() const
    {
        return m_cancelledPulls;
    }
    int cancelAllCount() const
    {
        return m_cancelAllCount;
    }
    /*! Finish all in-flight mutations (success by default); outcome and error can be given. */
    void completeMutations(MutationOutcome outcome = MutationOutcome::Succeeded, const DockerError &error = DockerError());
    /*! Finish only one target's mutation (e.g. one pull ends while the other keeps running). */
    void completeMutation(const QString &targetKey, MutationOutcome outcome, const DockerError &error = DockerError());
    /*! Simulate the engine pushing one pull progress update. */
    void emitPullProgress(const ImagePullProgress &progress);

    /*! Finish the current refresh round: emits *Updated / sectionFailed / loadingChanged. */
    void completeRefresh();

    int refreshCount(Section section) const;
    /*! Container ids still being sampled (verifies detail-page lifecycle, §27). */
    QSet<QString> samplingIds() const
    {
        return m_statsWanted;
    }

    // DockerBackendInterface
    void refreshEngine() override;
    void refreshContainers() override;
    void refreshImages() override;
    void refreshNetworks() override;

    /*! How often the network list was refreshed on demand (the create-container entry point must). */
    int networkRefreshCount() const
    {
        return m_networkRefreshCount;
    }
    void refreshVolumes(bool includeUsage = true) override;
    void pauseContainer(const QString &id) override;
    void unpauseContainer(const QString &id) override;
    void buildImage(const Kontainer::ImageBuildRequest &request) override;
    void pruneBuildCache() override;
    void completeBuildCachePrune(qint64 reclaimedBytes);
    void cancelImageBuild(const QString &buildId) override;
    void createContainer(const Kontainer::ContainerCreateRequest &request) override;

    /*! Last build request (asserts that query params and credential headers really get through). */
    ImageBuildRequest lastBuildRequest() const
    {
        return m_lastBuildRequest;
    }
    /*! Advance/finish one build (the real backend reads the stream; here the test feeds it). */
    void emitBuildProgress(const QString &buildId, const Kontainer::ImageBuildUpdate &update);
    void emitBuildFinished(const QString &buildId,
                           DockerBackendInterface::MutationOutcome outcome,
                           const Kontainer::DockerError &error = {},
                           const QString &imageId = {});
    QStringList cancelledBuilds() const
    {
        return m_cancelledBuilds;
    }
    void createVolume(const QString &name, const QString &driver = {}, const QList<QPair<QString, QString>> &labels = {}) override;

    /*! Last container-create request (asserts that form fields really get through). */
    ContainerCreateRequest lastContainerCreate() const
    {
        return m_lastContainerCreate;
    }
    /*! Deliver the id of a successful create (the real backend reads it, then emits containerCreated). */
    void completeContainerCreate(const QString &id, const QString &warning = {});
    void removeVolume(const QString &name) override;
    void pruneVolumes() override;

    /*! Name/driver of the last created volume and the last removed volume (asserts argument passing). */
    QString lastCreatedVolumeName() const
    {
        return m_lastCreatedVolume.first;
    }
    QString lastCreatedVolumeDriver() const
    {
        return m_lastCreatedVolume.second;
    }
    QString lastRemovedVolume() const
    {
        return m_lastRemovedVolume;
    }
    int pruneCallCount() const
    {
        return m_pruneCalls;
    }
    /*! Deliver prune's success detail (the real backend reads the removed list and reclaimed space). */
    void completePrune(const QStringList &names, qint64 reclaimedBytes);
    void refreshStorageUsage() override;
    void inspectContainer(const QString &id) override;
    void inspectImage(const QString &id) override;
    void requestContainerStats(const QString &id) override;
    void stopContainerStats(const QString &id) override;

    DockerEndpoint endpoint() const override;
    void startContainer(const QString &id) override;
    void stopContainer(const QString &id) override;
    void restartContainer(const QString &id) override;
    void removeContainer(const QString &id) override;
    void pullImage(const QString &reference, const Kontainer::RegistryCredential &credential = {}) override;
    void createNetwork(const Kontainer::NetworkCreateRequest &request) override;
    void removeNetwork(const QString &id) override;
    void connectNetwork(const QString &networkId, const QString &containerId, const QStringList &aliases = {}) override;
    void disconnectNetwork(const QString &networkId, const QString &containerId, bool force = false) override;

    /*! Last connect/disconnect request (asserts argument passing). */
    QPair<QString, QString> lastNetworkConnect() const
    {
        return m_lastNetworkConnect;
    }
    QStringList lastNetworkConnectAliases() const
    {
        return m_lastNetworkConnectAliases;
    }
    QPair<QString, QString> lastNetworkDisconnect() const
    {
        return m_lastNetworkDisconnect;
    }
    void cancelImagePull(const QString &reference) override;
    void cancelAllImagePulls() override;
    void removeImage(const QString &id, bool force) override;
    void checkRegistryAuth(const QString &serverAddress, const Kontainer::RegistryCredential &credential) override;
    void startContainerLogs(const QString &id, bool tty, bool follow, int tailLines) override;
    void stopContainerLogs(const QString &id) override;

    /* --- Log stream injection and observation (ARCH_V5_V8 §3.1) --- */

    /*! Emit a chunk of log lines as if the engine pushed them. */
    void emitLogLines(const QString &id, const QList<Kontainer::LogLine> &lines);
    /*! End the log stream (naturally ended by default). */
    void finishLogs(const QString &id, LogStreamEnd end = LogStreamEnd::Ended, const Kontainer::DockerError &error = {});
    /*! Arguments of the last startContainerLogs call. */
    QString lastLogContainerId() const
    {
        return m_lastLogContainerId;
    }
    bool lastLogTty() const
    {
        return m_lastLogTty;
    }
    bool lastLogFollow() const
    {
        return m_lastLogFollow;
    }
    int lastLogTailLines() const
    {
        return m_lastLogTailLines;
    }
    int stopLogsCount(const QString &id) const
    {
        return m_stoppedLogStreams.value(id);
    }

    /* --- Auth check injection and observation (ARCH_V5_V8 §2.6) --- */

    /*! Set the result of one auth check (success by default). */
    void setAuthCheckResult(AuthCheckResult result, const QString &detail = {});
    /*!
     * Whether to defer the auth check response (default false = answer immediately).
     *
     * Only enable it for cases that must observe the "checking" state; otherwise every case has to
     * remember a `completeAuthCheck()`, which easily produces false passes.
     */
    void setAuthCheckDeferred(bool deferred)
    {
        m_authCheckDeferred = deferred;
    }
    /*! Emit the queued auth check result (simulates the engine answering only afterwards). */
    void completeAuthCheck();
    QString lastAuthServerAddress() const
    {
        return m_lastAuthServerAddress;
    }
    RegistryCredential lastAuthCredential() const
    {
        return m_lastAuthCredential;
    }
    int authCheckCount() const
    {
        return m_authCheckCount;
    }
    /*! Credential attached to the last pull (asserts that wallet credentials reach the pull path). */
    RegistryCredential lastPullCredential() const
    {
        return m_lastPullCredential;
    }
    bool isLoading() const override;
    /*! Watchdog fallback: fail every pending request (same semantics as the real backend). */
    void abandonInFlightRequests(const Kontainer::DockerError &error) override;

    /*! Make requests never complete, to exercise the watchdog (they complete immediately otherwise). */
    void setStallRequests(bool stall)
    {
        m_stallRequests = stall;
    }
    bool isRefreshingFastData() const override;
    QString endpointDisplayName() const override;
    EngineInfo engineInfo() const override;
    QList<Container> containers() const override;
    QList<Network> networks() const override
    {
        return m_networks;
    }
    QList<Volume> volumes() const override
    {
        return m_volumes;
    }
    QList<Image> images() const override;
    StorageUsage storageUsage() const override;
    ContainerDetail containerDetail() const override;
    ImageDetail imageDetail() const override;
    ContainerStats containerStats() const override;
    bool isSamplingStats(const QString &id) const override;

private:
    void beginRefresh(Section section);

    EngineInfo m_engine;
    QList<Container> m_containers;
    QList<Image> m_images;
    QList<Network> m_networks;
    QList<Volume> m_volumes;
    bool m_lastVolumesIncludeUsage = true;
    QPair<QString, QString> m_lastCreatedVolume; // name, driver
    QString m_lastRemovedVolume;
    int m_pruneCalls = 0;
    NetworkCreateRequest m_lastNetworkCreate;
    QString m_lastRemovedNetwork;
    ContainerCreateRequest m_lastContainerCreate;
    ImageBuildRequest m_lastBuildRequest;
    QStringList m_cancelledBuilds;
    int m_networkRefreshCount = 0;
    QPair<QString, QString> m_lastNetworkConnect;
    QStringList m_lastNetworkConnectAliases;
    QPair<QString, QString> m_lastNetworkDisconnect;
    StorageUsage m_storageUsage;
    ContainerDetail m_containerDetail;
    QHash<QString, ContainerDetail> m_containerDetailsById;
    ImageDetail m_imageDetail;
    ContainerStats m_containerStats;
    QString m_endpointName = QStringLiteral("unix:///mock/docker.sock");
    QSet<QString> m_statsWanted;
    AuthCheckResult m_authCheckResult = AuthCheckResult::Succeeded;
    QString m_authCheckDetail;
    bool m_authCheckPending = false;
    bool m_authCheckDeferred = false;
    QString m_lastAuthServerAddress;
    RegistryCredential m_lastAuthCredential;
    int m_authCheckCount = 0;
    RegistryCredential m_lastPullCredential;
    QString m_lastLogContainerId;
    bool m_lastLogTty = false;
    bool m_lastLogFollow = false;
    int m_lastLogTailLines = 0;
    QHash<QString, int> m_stoppedLogStreams;

    bool m_loading = false;
    bool m_stallRequests = false;
    QHash<int, bool> m_pending;
    QHash<int, DockerError> m_failures;
    QHash<int, int> m_refreshCounts;

    DockerEndpoint m_endpoint;
    QList<MutationCall> m_mutationCalls;
    QStringList m_cancelledPulls;
    int m_cancelAllCount = 0;
};

} // namespace Kontainer
