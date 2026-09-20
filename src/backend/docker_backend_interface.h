/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/docker_endpoint.h"
#include "backend/docker_error.h"
#include "backend/log_frame_reader.h"
#include "backend/registry_auth.h"
#include "domain/container.h"
#include "domain/container_create_request.h"
#include "domain/container_detail.h"
#include "domain/container_stats.h"
#include "domain/engine_info.h"
#include "domain/image.h"
#include "domain/image_build.h"
#include "domain/network.h"
#include "domain/volume.h"
#include "domain/image_detail.h"
#include "domain/image_pull_progress.h"
#include "domain/storage_usage.h"

#include <QList>
#include <QObject>
#include <QString>

namespace Kontainer
{

/*!
 * Sole construction point for mutation target keys (ARCH_V4 §2.2.4).
 *
 * Backend and model must share one prefix scheme: the UI calls `isTargetBusy(key)`, so any
 * divergence in spelling silently breaks the busy state.
 */
namespace OperationTarget
{
inline QString container(const QString &id)
{
    return QStringLiteral("container:") + id;
}
inline QString image(const QString &reference)
{
    return QStringLiteral("image:") + reference;
}
/*! Volume: the name is the identity (Docker volumes have no separate id). */
inline QString volume(const QString &name)
{
    return QStringLiteral("volume:") + name;
}
/*! Volume prune targets no single object, but still needs busy-target tracking. */
inline QString volumePrune()
{
    return QStringLiteral("volume-prune");
}
/*! Network: name or id both work (creation has only a name, deletion uses the id). */
inline QString network(const QString &nameOrId)
{
    return QStringLiteral("network:") + nameOrId;
}
} // namespace OperationTarget

/*!
 * Backend abstraction (ARCH_V1 §30 / ARCH_V2 §42).
 *
 * DockerBackend talks to the Docker Engine; MockDockerBackend (tests/support/) supplies
 * controlled data so model/UI tests need no real daemon.
 *
 * The API states what to read, not which HTTP request to send (ARCH_V1 §11). Phase-2 calls
 * are read-only, and the backend knows no page/navigation state (ARCH_V2 §43).
 */
class DockerBackendInterface : public QObject
{
    Q_OBJECT

public:
    /*! Section (ARCH_V2 §30): one dataset's failure affects only its section. */
    enum class Section {
        Engine,
        Containers,
        Images,
        Storage,
        ContainerDetail,
        ImageDetail,
        Stats,
        /*! Network list (phase 6, §3.2). */
        Networks,
        /*! Volume list (phase 6, §3.5). */
        Volumes,
    };
    Q_ENUM(Section)

    /*! Mutations (ARCH_V4 §2.2.4 / §2.3 / §2.4). */
    enum class Mutation {
        StartContainer,
        StopContainer,
        RestartContainer,
        RemoveContainer,
        PullImage,
        RemoveImage,
        /*! Phase 6: create/remove networks (§3.3) and connect/disconnect containers (§3.4). */
        CreateNetwork,
        RemoveNetwork,
        ConnectNetwork,
        DisconnectNetwork,
        /*! Pause/unpause a running container (user testing feedback ①). */
        PauseContainer,
        UnpauseContainer,
        /*! Phase 7: create a container (§4.6). */
        CreateContainer,
        /*! Phase 8: build an image from a Dockerfile (§5.3). */
        BuildImage,
        /*! Phase 8: prune the build cache (§5.5). */
        PruneBuildCache,
        /*! Phase 6: create/remove/prune volumes (§3.5). */
        CreateVolume,
        RemoveVolume,
        PruneVolumes,
    };
    Q_ENUM(Mutation)

    /*!
     * Registry credential check result (ARCH_V5_V8 §2.6).
     *
     * Four classes instead of success/failure: each needs a different next step — bad
     * credentials mean re-enter them, an unreachable registry means check network/proxy, and
     * an engine-side failure is not user-fixable.
     */
    enum class AuthCheckResult {
        Succeeded,
        /*! 401/403: wrong user name, password or token. */
        InvalidCredentials,
        /*! The engine cannot reach the registry (DNS / refused / TLS / proxy / timeout). */
        RegistryUnreachable,
        /*! Other failures (engine 5xx, unparsable response, …). */
        Failed,
    };
    Q_ENUM(AuthCheckResult)

    /*! Cancellation is not an error, so DockerError alone cannot express the result (ARCH_V4 §2.2.1). */
    enum class MutationOutcome {
        Succeeded,
        /*! Engine returned 304: already in the target state (repeat start/stop). */
        Unchanged,
        Failed,
        Cancelled,
    };
    Q_ENUM(MutationOutcome)

    explicit DockerBackendInterface(QObject *parent = nullptr);
    ~DockerBackendInterface() override;

    /* --- High-frequency datasets --- */
    virtual void refreshEngine() = 0;
    virtual void refreshContainers() = 0;
    virtual void refreshImages() = 0;
    /*! Network list (phase 6). Low-frequency (create/remove/connect/disconnect); refresh on demand. */
    virtual void refreshNetworks() = 0;
    /*!
     * Volume list (phase 6). Also low-frequency; refresh on demand.
     * `includeUsage` off skips usage scanning (fast, no size/ref count) — for names-only UIs.
     */
    virtual void refreshVolumes(bool includeUsage = true) = 0;
    /*! Refresh the high-frequency datasets together (Engine + Containers + Images). */
    virtual void refreshAll();

    /* --- Medium-frequency / on-demand datasets --- */
    /*! Engine-wide disk usage (`GET /system/df`). */
    virtual void refreshStorageUsage() = 0;
    /*! Container inspect (`GET /containers/{id}/json`). */
    virtual void inspectContainer(const QString &id) = 0;
    /*! Image inspect (`GET /images/{id}/json`). */
    virtual void inspectImage(const QString &id) = 0;
    /*! One-off container stats sample (`GET /containers/{id}/stats?stream=false`). */
    virtual void requestContainerStats(const QString &id) = 0;
    /*!
     * Drop interest in a container's stats (ARCH_V2 §27: leaving the detail page must stop it).
     * This only ends the local request lifetime; it is not a Docker mutation.
     */
    virtual void stopContainerStats(const QString &id) = 0;

    /* --- Mutations (ARCH_V4 §2.2.4) ---------------------------------------------
     *
     * They express what to change, not HTTP details; only OperationController calls these,
     * never QML (ARCH_V4 §1.5 chokepoint). Each emits mutationFinished() exactly once;
     * a pull additionally emits several imagePullProgress().
     */
    virtual void startContainer(const QString &id) = 0;
    /*! `t` always comes from RefreshPolicy::kStopTimeoutSeconds, never from the UI. */
    virtual void stopContainer(const QString &id) = 0;
    virtual void restartContainer(const QString &id) = 0;
    /*! Remove container; no `v` (keep volumes), no `force` (engine rejects a running one). */
    virtual void removeContainer(const QString &id) = 0;
    /*!
     * Pull an image (ARCH_V4 §2.4).
     *
     * Concurrent: distinct references may be in flight independently; a repeated pull of the
     * same reference is rejected here (the engine deduplicates too, but rejecting locally
     * yields a clearer message).
     */
    /*!
     * Pull an image.
     *
     * `credential` belongs to the registry holding that image (empty = anonymous pull).
     * It is used only for this request's `X-Registry-Auth` header: never stored, never logged.
     */
    virtual void pullImage(const QString &reference, const Kontainer::RegistryCredential &credential = {}) = 0;
    /*! Cancel an in-flight pull; a no-op if none matches. */
    virtual void cancelImagePull(const QString &reference) = 0;
    /*! Cancel all in-flight pulls (fallback when the KCM closes/exits). */
    virtual void cancelAllImagePulls() = 0;
    /*!
     * Abandon all in-flight requests (last resort).
     *
     * Used when requests hang forever (half-dead daemon, socket accepted but silent): the UI
     * must not show "loading / backend busy" indefinitely. A controller watchdog calls this to
     * fail in-flight state and queued callbacks as timeouts, leaving the UI retryable.
     */
    virtual void abandonInFlightRequests(const Kontainer::DockerError &error) = 0;

    /*! Cancel one build (phase 8 §5.3): aborts upload/response; the backend cleans the temp context. */
    virtual void cancelImageBuild(const QString &buildId) = 0;
    /*! `force=true` forces deletion of multi-tagged images (the engine demands it on 409). */
    virtual void removeImage(const QString &id, bool force) = 0;

    /*!
     * Create a network (ARCH_V5_V8 §3.3).
     *
     * bridge only for now: the request carries `driver`, but the UI allows bridge alone —
     * other drivers are recognized, never created (deliberate deviation, see §3.3).
     */
    virtual void createNetwork(const Kontainer::NetworkCreateRequest &request) = 0;
    /*!
     * Prune the build cache (`POST /build/prune`, §5.5).
     *
     * The UI estimates reclaimable space from BuildCache in `GET /system/df` (see `StorageUsage`);
     * the actual reclaimed bytes arrive via `buildCachePruned()`.
     */
    virtual void pruneBuildCache() = 0;

    /*! Pause a running container (`POST /containers/{id}/pause`). */
    virtual void pauseContainer(const QString &id) = 0;
    /*! Unpause a paused container (`POST /containers/{id}/unpause`). */
    virtual void unpauseContainer(const QString &id) = 0;

    /*!
     * Build an image (`POST /build`, §5.3).
     *
     * The caller packs the context with `packBuildContext()` (`request.contextArchive`); the
     * backend uploads it, parses the line-by-line JSON progress, and deletes the temp tar when done.
     */
    virtual void buildImage(const Kontainer::ImageBuildRequest &request) = 0;

    /*!
     * Create a container (`POST /containers/create?name=…`, §4.6).
     *
     * Form-to-body mapping lives entirely in `ContainerCreateRequest::toJson()` (single
     * implementation + snapshot tests). The new id arrives via `containerCreated()` —
     * `mutationFinished` carries only the error and cannot hold it.
     */
    virtual void createContainer(const Kontainer::ContainerCreateRequest &request) = 0;

    /*!
     * Create a volume (`POST /volumes/create`, §3.5).
     * Name validation and duplicate checks happen in the controller; this only sends the request
     * (`Driver` defaults to local).
     */
    virtual void createVolume(const QString &name, const QString &driver = {}, const QList<QPair<QString, QString>> &labels = {}) = 0;
    /*!
     * Remove a volume (`DELETE /volumes/{name}`).
     * No force: the engine refuses while a container uses it, and the UI explains why instead of
     * force-deleting for the user.
     */
    virtual void removeVolume(const QString &name) = 0;
    /*!
     * Prune unused volumes (`POST /volumes/prune`, §3.5).
     *
     * The result (which volumes, how much reclaimed) arrives via `volumesPruned()` —
     * `mutationFinished` carries only the error and cannot hold that success detail.
     */
    virtual void pruneVolumes() = 0;

    /*! Remove a network (`DELETE /networks/{id}`). The daemon rejects built-in ones (403). */
    virtual void removeNetwork(const QString &id) = 0;

    /*!
     * Connect a container to a network (`POST /networks/{id}/connect`, §3.4).
     *
     * `aliases` are this container's names on that network (Docker `EndpointConfig.Aliases`):
     * peers on the same network reach each other by alias, which is stabler than IP.
     */
    virtual void connectNetwork(const QString &networkId, const QString &containerId, const QStringList &aliases = {}) = 0;
    /*!
     * Disconnect a container from a network (`POST /networks/{id}/disconnect`, §3.4).
     * `force` defaults off: the daemon needs it only for running containers, and silently forcing
     * a disconnect is not our intended default.
     */
    virtual void disconnectNetwork(const QString &networkId, const QString &containerId, bool force = false) = 0;

    /*!
     * Why a log stream ended (ARCH_V5_V8 §3.1.4).
     *
     * Three classes, not success/failure: ending because the container stopped is normal
     * (reconnect allowed), a user cancel is not an error — only `Failed` needs error text.
     */
    enum class LogStreamEnd {
        /*! Stream ended naturally (container stopped, history exhausted). */
        Ended,
        /*! Read failed (no such container, log driver cannot read, connection dropped, …). */
        Failed,
        /*! User cancelled (left the log section / hit stop). */
        Cancelled,
    };
    Q_ENUM(LogStreamEnd)

    /*!
     * Start reading logs.
     *
     * `tty` must come from the container detail (`Config.Tty`): TTY containers emit raw bytes,
     * non-TTY uses an 8-byte-frame stdcopy stream (§3.1.1). With `follow` set it keeps following
     * and applies **no silence timeout** (a follow stream may legitimately idle for long).
     */
    virtual void startContainerLogs(const QString &id, bool tty, bool follow, int tailLines) = 0;
    /*! Stop reading (idempotent; no error when no stream is running). */
    virtual void stopContainerLogs(const QString &id) = 0;

    /*!
     * Check one registry credential (`POST /auth`).
     *
     * The credential travels **only in the request header**, only for this request: never
     * persisted, logged, or in error text. `serverAddress` names the registry to check and fills
     * the header's `serveraddress` field (the struct's same-named field is a fallback when the
     * argument is empty). The result arrives asynchronously via `registryAuthChecked()`.
     */
    virtual void checkRegistryAuth(const QString &serverAddress, const Kontainer::RegistryCredential &credential) = 0;

    /*! Current endpoint: the permission gate (DockerCapabilities) derives writability from it. */
    virtual DockerEndpoint endpoint() const = 0;

    /*! Whether any request is in flight. */
    virtual bool isLoading() const = 0;
    /*!
     * Whether a high-frequency dataset (Engine/Containers/Images) request is in flight.
     * Auto-refresh must be blocked only by these, not held up by details/sampling (ARCH_V2 §13.2).
     */
    virtual bool isRefreshingFastData() const = 0;
    /*! Display name of the current endpoint (unix:///...). */
    virtual QString endpointDisplayName() const = 0;

    /*! Last successfully read domain data; models read it after the matching *Updated() signal. */
    virtual EngineInfo engineInfo() const = 0;
    virtual QList<Container> containers() const = 0;
    virtual QList<Image> images() const = 0;
    virtual QList<Network> networks() const = 0;
    virtual QList<Volume> volumes() const = 0;
    virtual StorageUsage storageUsage() const = 0;
    virtual ContainerDetail containerDetail() const = 0;
    virtual ImageDetail imageDetail() const = 0;
    virtual ContainerStats containerStats() const = 0;
    /*! Whether a container's stats are still being sampled. */
    virtual bool isSamplingStats(const QString &id) const = 0;

Q_SIGNALS:
    void engineUpdated();
    void containersUpdated();
    void imagesUpdated();
    void networksUpdated();
    void volumesUpdated();
    /*! Build cache pruned: reclaimed bytes. */
    void buildCachePruned(qint64 reclaimedBytes);
    /*! Build progress: `update` is this line's increment (step, raw status text, progress). */
    void imageBuildProgress(const QString &buildId, const Kontainer::ImageBuildUpdate &update);
    /*! Build finished: image id on success; on failure `error` names the failing step. */
    void imageBuildFinished(const QString &buildId,
                            Kontainer::DockerBackendInterface::MutationOutcome outcome,
                            const Kontainer::DockerError &error,
                            const QString &imageId);
    /*! Container created: the new id plus any engine warning (`Warnings`, e.g. a truncated name). */
    void containerCreated(const QString &id, const QString &warning);
    /*! Volumes pruned: removed names and reclaimed bytes (empty = nothing to prune). */
    void volumesPruned(const QStringList &names, qint64 reclaimedBytes);
    void storageUpdated();
    void containerDetailUpdated();
    void imageDetailUpdated();
    void containerStatsUpdated();
    void loadingChanged();
    /*! Failure of one dataset; every backend failure must be observable (ARCH_V1 §23.1). */
    void sectionFailed(Kontainer::DockerBackendInterface::Section section, const Kontainer::DockerError &error);

    /*!
     * End of one mutation (succeeded / failed / cancelled).
     * `targetKey` matches OperationController's: `container:<id>` / `image:<ref>`.
     */
    void mutationFinished(Kontainer::DockerBackendInterface::Mutation mutation,
                          const QString &targetKey,
                          Kontainer::DockerBackendInterface::MutationOutcome outcome,
                          const Kontainer::DockerError &error);
    /*! Pull progress (aggregated once per engine-reported line). */
    void imagePullProgress(const Kontainer::ImagePullProgress &progress);

    /*! Already demultiplexed log lines (emitted in batches so the UI is not woken per byte). */
    void containerLogLines(const QString &id, const QList<Kontainer::LogLine> &lines);
    /*! Log stream finished (why; on `Failed`, `error` is the engine/transport classification). */
    void containerLogsFinished(const QString &id,
                               Kontainer::DockerBackendInterface::LogStreamEnd end,
                               const Kontainer::DockerError &error);

    /*!
     * Registry credential check result.
     *
     * `detail` is the engine's raw text, for logs and "technical details" only (it may contain
     * registry-supplied text), never user-facing copy — the UI picks its copy from `result`.
     */
    void registryAuthChecked(const QString &serverAddress,
                             Kontainer::DockerBackendInterface::AuthCheckResult result,
                             const QString &detail);
};

} // namespace Kontainer
