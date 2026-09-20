/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/docker_backend_interface.h"
#include "backend/docker_client.h"
#include "backend/http/json_line_reader.h"
#include "domain/container.h"
#include "domain/container_detail.h"
#include "domain/container_stats.h"
#include "domain/engine_info.h"
#include "domain/image.h"
#include "domain/network.h"
#include "domain/volume.h"
#include "domain/image_detail.h"
#include "domain/image_pull_progress.h"
#include "domain/storage_usage.h"
#include "dto/engine_dto.h"

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QSet>

#include <functional>

namespace Kontainer
{

/*!
 * Real Docker backend (ARCH_V1 §6.3/§11/§15/§17, ARCH_V2 §29/§42, ARCH_V4 §2.2).
 *
 * Owns HTTP requests, the Unix socket, HTTP status codes, JSON decoding, API version
 * handling, error mapping and request deduplication. No UI text, and no knowledge of
 * which page the user is on (§43).
 *
 * Read paths: GET /_ping, /version, /info, /containers/json, /images/json, /system/df,
 * /containers/{id}/json, /images/{id}/json, /containers/{id}/stats?stream=false.
 *
 * Since phase 4 also writes (ARCH_V4 §2.3/§2.4): container start / stop / restart /
 * remove, image pull (streaming, with progress and cancel) / remove. All paths come from
 * `docker_api_paths.h`; this is the only file allowed to call DockerClient write methods
 * (asserted by tst_source_conventions).
 */
class DockerBackend : public DockerBackendInterface
{
    Q_OBJECT

public:
    explicit DockerBackend(QObject *parent = nullptr);
    ~DockerBackend() override;

    void setEndpoint(const DockerEndpoint &endpoint);
    void setTimeoutMs(int timeoutMs);

    void refreshEngine() override;
    void refreshContainers() override;
    void refreshImages() override;
    void refreshNetworks() override;
    void refreshVolumes(bool includeUsage = true) override;
    void buildImage(const Kontainer::ImageBuildRequest &request) override;
    void cancelImageBuild(const QString &buildId) override;
    void pruneBuildCache() override;
    void abandonInFlightRequests(const Kontainer::DockerError &error) override;
    void createContainer(const Kontainer::ContainerCreateRequest &request) override;
    void createVolume(const QString &name, const QString &driver = {}, const QList<QPair<QString, QString>> &labels = {}) override;
    void removeVolume(const QString &name) override;
    void pruneVolumes() override;

    void refreshStorageUsage() override;
    void inspectContainer(const QString &id) override;
    void inspectImage(const QString &id) override;
    void requestContainerStats(const QString &id) override;
    void stopContainerStats(const QString &id) override;

    /* --- Write operations (ARCH_V4 §2.2.4) --- */
    DockerEndpoint endpoint() const override;
    void startContainer(const QString &id) override;
    void stopContainer(const QString &id) override;
    void restartContainer(const QString &id) override;
    void pauseContainer(const QString &id) override;
    void unpauseContainer(const QString &id) override;
    void removeContainer(const QString &id) override;
    void pullImage(const QString &reference, const Kontainer::RegistryCredential &credential = {}) override;
    void createNetwork(const Kontainer::NetworkCreateRequest &request) override;
    void removeNetwork(const QString &id) override;
    void connectNetwork(const QString &networkId, const QString &containerId, const QStringList &aliases = {}) override;
    void disconnectNetwork(const QString &networkId, const QString &containerId, bool force = false) override;
    void cancelImagePull(const QString &reference) override;
    void cancelAllImagePulls() override;
    void removeImage(const QString &id, bool force) override;

    void checkRegistryAuth(const QString &serverAddress, const Kontainer::RegistryCredential &credential) override;

    void startContainerLogs(const QString &id, bool tty, bool follow, int tailLines) override;
    void stopContainerLogs(const QString &id) override;

    bool isLoading() const override;
    bool isRefreshingFastData() const override;
    QString endpointDisplayName() const override;

    EngineInfo engineInfo() const override
    {
        return m_engine;
    }
    QList<Container> containers() const override
    {
        return m_containers;
    }
    QList<Network> networks() const override
    {
        return m_networks;
    }
    QList<Volume> volumes() const override
    {
        return m_volumes;
    }
    QList<Image> images() const override
    {
        return m_images;
    }
    StorageUsage storageUsage() const override
    {
        return m_storageUsage;
    }
    ContainerDetail containerDetail() const override
    {
        return m_containerDetail;
    }
    ImageDetail imageDetail() const override
    {
        return m_imageDetail;
    }
    ContainerStats containerStats() const override
    {
        return m_containerStats;
    }
    bool isSamplingStats(const QString &id) const override;

private:
    using ReadyCallback = std::function<void()>;

    /*! `/auth` timeout (the engine contacts the registry; same order as a mutation). */
    int authCheckTimeoutMs() const;


    /* --- Container logs (streaming, ARCH_V5_V8 §3.1) --- */
    struct LogStreamState {
        DockerReply *reply = nullptr;
        LogFrameReader reader;
        /*! Whether the user already asked to stop (so the end reason is reported as cancel). */
        bool cancelled = false;
    };
    /*! At most one log stream per container (switching or reconnecting stops the old one). */
    QHash<QString, LogStreamState> m_logStreams;
    /*! Timeout for history reads (follow=0); follow streams get no idle timeout. */
    int logHistoryTimeoutMs() const;
    /*! Log requests cancelled while still awaiting the version handshake (never streamed). */
    QSet<QString> m_cancelledLogRequests;

    /* --- Image pulls (streaming, concurrent, ARCH_V4 §2.4) --- */

    struct PullLayerState {
        qint64 current = 0;
        qint64 total = 0;
        bool complete = false;
    };

    /*! All state of one pull: it has its own stream parser and progress aggregation. */
    struct ImagePullState {
        QString reference;
        QString targetKey;
        DockerReply *reply = nullptr;
        JsonLineReader reader;
        ImagePullProgress progress;
        QHash<QString, PullLayerState> layers;
        bool failed = false;
    };

    /*! State of one build: its own stream parser plus the current progress. */
    struct ImageBuildState {
        QString id;
        QString contextArchive;
        DockerReply *reply = nullptr;
        JsonLineReader reader;
        ImageBuildUpdate update;
        bool failed = false;
    };

    /*! Requests needing an API version prefix: finish the handshake first when unnegotiated. */
    void withApiVersion(Section section, ReadyCallback callback);

    /*!
     * Base for writes: run after the version handshake. If the handshake failed, the error
     * is reported as a mutation failure (not as an unrelated section error).
     */
    void runMutation(Mutation mutation, const QString &targetKey, ReadyCallback run);
    /*! Shared implementation of start / stop / restart / remove. */
    void runContainerMutation(Mutation mutation, const QString &id, const QString &apiPath, const QUrlQuery &query);
    /*!
     * Aggregate each network's members from `NetworkSettings.Networks` in the container list.
     *
     * Measured: `GET /networks` returns an empty `Containers` field (only
     * `GET /networks/{id}` fills it), so membership can only come from the container side.
     * Recompute when either list arrives (idempotent, cheap).
     */
    void refreshNetworkMembership();

    /*! Handle one line of a build stream (aggregate progress, record the failure reason). */
    void handleBuildLine(ImageBuildState &state, const QJsonObject &object);
    /*! End one build: delete the temp tar, clean up state, emit signals. */
    void finishBuild(const QString &buildId, MutationOutcome outcome, const DockerError &error);

    void startPullRequest(const QString &reference, const QString &targetKey, const Kontainer::RegistryCredential &credential);
    void handlePullLine(ImagePullState &state, const QJsonObject &object);
    void updatePullTotals(ImagePullState &state);
    void finishPull(const QString &targetKey, MutationOutcome outcome, const DockerError &error);
    void emitPullProgress(const ImagePullState &state);
    void emitMutationFinished(Mutation mutation, const QString &targetKey, MutationOutcome outcome, const DockerError &error);

    void startPing();
    void startVersionRequest();
    void startInfoRequest();
    void startContainersRequest();
    void startImagesRequest();
    void startNetworksRequest();
    void startVolumesRequest(bool includeUsage);
    void startStorageRequest();
    void startContainerInspectRequest(const QString &id);
    void startImageInspectRequest(const QString &id);
    void startStatsRequest(const QString &id);

    void finishHandshakeSuccess();
    void finishHandshakeFailure(const DockerError &error);
    /*! Clear the /info aggregate counts so the previous read's values are not shown as current. */
    void resetEngineCounts();
    void flushReadyCallbacks(const DockerError &error);
    void failSection(Section section, const DockerError &error);
    void updateLoading();

    /*!
     * Request deduplication (ARCH_V2 §29): key = request type + resource.
     * Returns false when a same-type request for that resource is already in flight; the
     * caller should coalesce this one.
     */
    bool beginRequest(const QString &key);
    void endRequest(const QString &key);

    DockerClient m_client;
    EngineInfo m_engine;
    QList<Container> m_containers;
    QList<Image> m_images;
    StorageUsage m_storageUsage;
    ContainerDetail m_containerDetail;
    ImageDetail m_imageDetail;
    ContainerStats m_containerStats;

    bool m_engineInFlight = false;
    bool m_containersInFlight = false;
    bool m_imagesInFlight = false;
    bool m_networksInFlight = false;
    bool m_volumesInFlight = false;
    /*! Last successfully read volume list (kept when a refresh fails). */
    QList<Volume> m_volumes;
    /*! Last successfully read network list (kept on failure so the UI does not go blank). */
    QList<Network> m_networks;
    bool m_handshakeInFlight = false;
    bool m_loading = false;

    /*! Set of in-flight request keys (deduplication for inspect / stats / storage). */
    QSet<QString> m_inFlightRequests;
    /*! Containers still wanting stats (removed when leaving the detail page, §27). */
    QSet<QString> m_statsWanted;

    QList<QPair<Section, ReadyCallback>> m_readyCallbacks;

    /*! In-flight pulls, key = targetKey (`image:<normalized reference>`). */
    QHash<QString, ImagePullState> m_pulls;
    QHash<QString, ImageBuildState> m_builds;

    /*! Writes waiting for the version handshake (settled together once it ends). */
    struct PendingMutation {
        Mutation mutation;
        QString targetKey;
        ReadyCallback run;
    };
    QList<PendingMutation> m_pendingMutations;

};

} // namespace Kontainer
