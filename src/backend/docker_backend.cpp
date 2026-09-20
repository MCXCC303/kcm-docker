/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/docker_backend.h"

#include "backend/docker_api_paths.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "dto/container_dto.h"
#include "dto/container_inspect_dto.h"
#include "dto/image_dto.h"
#include "dto/network_dto.h"
#include "dto/image_inspect_dto.h"
#include "dto/image_build_dto.h"
#include "dto/image_pull_dto.h"

#include <QFileInfo>
#include "dto/stats_dto.h"
#include "dto/storage_dto.h"
#include "dto/volume_dto.h"
#include "domain/image_reference.h"
#include "logging.h"

#include <KLocalizedString>
#include "refresh_policy.h"

#include <QUrlQuery>

#include <chrono>

namespace Kontainer
{

using Section = DockerBackendInterface::Section;

DockerBackend::DockerBackend(QObject *parent)
    : DockerBackendInterface(parent)
{
}

DockerBackend::~DockerBackend() = default;

void DockerBackend::setEndpoint(const DockerEndpoint &endpoint)
{
    m_client.setEndpoint(endpoint);
    m_client.clearApiVersion();
}

void DockerBackend::setTimeoutMs(int timeoutMs)
{
    m_client.setTimeoutMs(timeoutMs);
}

bool DockerBackend::isLoading() const
{
    return m_engineInFlight || m_containersInFlight || m_imagesInFlight || !m_inFlightRequests.isEmpty();
}

bool DockerBackend::isRefreshingFastData() const
{
    return m_engineInFlight || m_containersInFlight || m_imagesInFlight;
}

bool DockerBackend::isSamplingStats(const QString &id) const
{
    return m_statsWanted.contains(id);
}

bool DockerBackend::beginRequest(const QString &key)
{
    if (m_inFlightRequests.contains(key)) {
        // Same request type already in flight for this resource: coalesce (ARCH_V2 §29)
        qCDebug(kontainerBackend) << "coalesced request" << key;
        return false;
    }
    m_inFlightRequests.insert(key);
    updateLoading();
    return true;
}

void DockerBackend::endRequest(const QString &key)
{
    if (m_inFlightRequests.remove(key)) {
        updateLoading();
    }
}

QString DockerBackend::endpointDisplayName() const
{
    return m_client.endpoint().displayName();
}

void DockerBackend::updateLoading()
{
    const bool loading = isLoading();
    if (loading == m_loading) {
        return;
    }
    m_loading = loading;
    Q_EMIT loadingChanged();
}

/* ------------------------------------------------------------------------- */
/* Engine: /_ping → /version (negotiate API version) → /info                 */
/* ------------------------------------------------------------------------- */

void DockerBackend::refreshEngine()
{
    if (m_engineInFlight) {
        // Deduplication: never create a second request for the same dataset (ARCH_V1 §17)
        qCDebug(kontainerBackend) << "coalesced engine refresh";
        return;
    }
    m_engineInFlight = true;
    updateLoading();

    if (m_handshakeInFlight) {
        return; // a handshake is already in flight (possibly triggered by containers/images)
    }
    m_handshakeInFlight = true;
    startPing();
}

void DockerBackend::startPing()
{
    DockerReply *reply = m_client.getUnversioned(ApiPaths::ping());
    connect(reply, &DockerReply::finished, this, [this, reply] {
        const bool failed = reply->state() != DockerReply::State::Succeeded;
        const DockerError error = reply->error();
        reply->deleteLater();

        if (failed) {
            finishHandshakeFailure(error);
            return;
        }
        startVersionRequest();
    });
}

void DockerBackend::startVersionRequest()
{
    DockerReply *reply = m_client.getUnversioned(ApiPaths::version());
    connect(reply, &DockerReply::finished, this, [this, reply] {
        const bool failed = reply->state() != DockerReply::State::Succeeded;
        const DockerError error = reply->error();
        const QByteArray body = reply->body();
        reply->deleteLater();

        if (failed) {
            finishHandshakeFailure(error);
            return;
        }

        QString parseError;
        const auto version = DockerVersionDTO::fromPayload(body, &parseError);
        if (!version.has_value()) {
            finishHandshakeFailure(DockerError(DockerError::Kind::UnexpectedPayload, parseError));
            return;
        }

        const auto serverVersion = ApiVersion::fromString(version->apiVersion);
        if (!serverVersion.has_value()) {
            finishHandshakeFailure(DockerError(DockerError::Kind::ApiVersionMismatch,
                                               QStringLiteral("unparsable server API version '%1'").arg(version->apiVersion)));
            return;
        }
        const auto serverMinimum = ApiVersion::fromString(version->minApiVersion);
        const auto negotiated = ApiVersion::negotiate(*serverVersion, serverMinimum);
        if (!negotiated.has_value()) {
            finishHandshakeFailure(DockerError(DockerError::Kind::ApiVersionMismatch,
                                               QStringLiteral("server API %1 (minimum %2) is outside the supported range %3..%4")
                                                   .arg(version->apiVersion, version->minApiVersion)
                                                   .arg(ApiVersion::clientMinMinor())
                                                   .arg(ApiVersion::clientMaxMinor())));
            return;
        }

        m_client.setApiVersion(*negotiated);
        qCDebug(kontainerBackend) << "negotiated Docker API version" << negotiated->toString();

        m_engine.available = true;
        m_engine.serverVersion = version->version;
        m_engine.apiVersion = negotiated->toString();
        m_engine.minApiVersion = version->minApiVersion;
        m_engine.osType = version->os;
        m_engine.architecture = version->arch;
        m_engine.kernelVersion = version->kernelVersion;
        // Component table (dockerd / containerd / runc …): also shown next to the engine version
        m_engine.components.clear();
        m_engine.components.reserve(version->components.size());
        for (const DockerComponentDTO &component : version->components) {
            EngineComponent entry;
            entry.name = component.name;
            entry.version = component.version;
            m_engine.components.append(entry);
        }

        startInfoRequest();
    });
}

void DockerBackend::startInfoRequest()
{
    DockerReply *reply = m_client.get(ApiPaths::info());
    connect(reply, &DockerReply::finished, this, [this, reply] {
        const bool failed = reply->state() != DockerReply::State::Succeeded;
        const DockerError error = reply->error();
        const QByteArray body = reply->body();
        reply->deleteLater();

        if (failed) {
            // Error isolation (§15): version info is usable and containers/images still load,
            // but the /info counts must be dropped or the UI shows the previous read's values.
            qCWarning(kontainerBackend) << "GET /info failed, engine summary counts are now unavailable";
            resetEngineCounts();
            finishHandshakeSuccess();
            Q_EMIT sectionFailed(Section::Engine, error);
            return;
        }

        QString parseError;
        const auto info = DockerInfoDTO::fromPayload(body, &parseError);
        if (!info.has_value()) {
            resetEngineCounts();
            finishHandshakeSuccess();
            Q_EMIT sectionFailed(Section::Engine, DockerError(DockerError::Kind::UnexpectedPayload, parseError));
            return;
        }

        m_engine.countsAvailable = true;
        m_engine.engineName = info->engineName;
        m_engine.operatingSystem = info->operatingSystem;
        m_engine.storageDriver = info->storageDriver;
        m_engine.cgroupVersion = info->cgroupVersion;
        m_engine.cgroupDriver = info->cgroupDriver;
        m_engine.cpuCount = info->cpus;
        m_engine.warnings = info->warnings;
        m_engine.containerTotal = info->containers;
        m_engine.containersRunning = info->containersRunning;
        m_engine.containersPaused = info->containersPaused;
        m_engine.containersStopped = info->containersStopped;
        m_engine.imageCount = info->images;
        m_engine.memoryTotalBytes = info->memoryTotalBytes;
        m_engine.securityOptions = info->securityOptions;
        m_engine.dockerRootDir = info->dockerRootDir;
        m_engine.loggingDriver = info->loggingDriver;
        m_engine.registryMirrors = info->registryMirrors;
        m_engine.liveRestoreEnabled = info->liveRestoreEnabled;
        // Prefer /info values over /version when they are present
        if (!info->kernelVersion.isEmpty()) {
            m_engine.kernelVersion = info->kernelVersion;
        }
        if (!info->architecture.isEmpty()) {
            m_engine.architecture = info->architecture;
        }
        if (!info->osType.isEmpty()) {
            m_engine.osType = info->osType;
        }

        finishHandshakeSuccess();
    });
}

void DockerBackend::resetEngineCounts()
{
    m_engine.countsAvailable = false;
    m_engine.engineName.clear();
    m_engine.operatingSystem.clear();
    m_engine.storageDriver.clear();
    m_engine.cgroupVersion.clear();
    m_engine.cgroupDriver.clear();
    m_engine.components.clear();
    m_engine.warnings.clear();
    m_engine.cpuCount = 0;
    m_engine.containerTotal = 0;
    m_engine.memoryTotalBytes = 0;
    m_engine.containersRunning = 0;
    m_engine.containersPaused = 0;
    m_engine.containersStopped = 0;
    m_engine.imageCount = 0;
}

void DockerBackend::finishHandshakeSuccess()
{
    m_handshakeInFlight = false;
    if (m_engineInFlight) {
        m_engineInFlight = false;
        updateLoading();
        Q_EMIT engineUpdated();
    }
    flushReadyCallbacks(DockerError());
}

void DockerBackend::finishHandshakeFailure(const DockerError &error)
{
    qCWarning(kontainerBackend) << "Docker handshake failed:" << error.detail();

    m_handshakeInFlight = false;
    m_client.clearApiVersion();
    m_engine = EngineInfo();
    m_engineInFlight = false;
    updateLoading();
    Q_EMIT engineUpdated();
    Q_EMIT sectionFailed(Section::Engine, error);

    // Handshake-dependent sections fail together but report separately (error isolation)
    flushReadyCallbacks(error);
}

void DockerBackend::flushReadyCallbacks(const DockerError &error)
{
    const QList<QPair<Section, ReadyCallback>> callbacks = std::exchange(m_readyCallbacks, {});
    for (const auto &[section, callback] : callbacks) {
        if (error.isError()) {
            failSection(section, error);
        } else {
            callback();
        }
    }

    // Writes waiting for the handshake: run on success, report a mutation failure otherwise
    const QList<PendingMutation> mutations = std::exchange(m_pendingMutations, {});
    for (const PendingMutation &pending : mutations) {
        if (error.isError()) {
            emitMutationFinished(pending.mutation, pending.targetKey, MutationOutcome::Failed, error);
        } else {
            pending.run();
        }
    }
}

void DockerBackend::failSection(Section section, const DockerError &error)
{
    switch (section) {
    case Section::Engine:
        m_engineInFlight = false;
        break;
    case Section::Containers:
        m_containersInFlight = false;
        break;
    case Section::Images:
        m_imagesInFlight = false;
        break;
    case Section::Networks:
        m_networksInFlight = false;
        break;
    case Section::Volumes:
        m_volumesInFlight = false;
        break;
    case Section::Storage:
    case Section::ContainerDetail:
    case Section::ImageDetail:
    case Section::Stats:
        // These sections track their in-flight state in m_inFlightRequests
        break;
    }
    updateLoading();
    Q_EMIT sectionFailed(section, error);
}

/* ------------------------------------------------------------------------- */
/* Containers / Images                                                        */
/* ------------------------------------------------------------------------- */

void DockerBackend::withApiVersion(Section section, ReadyCallback callback)
{
    if (m_client.hasApiVersion()) {
        callback();
        return;
    }
    m_readyCallbacks.append({section, std::move(callback)});
    if (!m_handshakeInFlight) {
        m_handshakeInFlight = true;
        m_engineInFlight = true; // the handshake also fills the Engine state
        updateLoading();
        startPing();
    }
}

void DockerBackend::refreshContainers()
{
    if (m_containersInFlight) {
        qCDebug(kontainerBackend) << "coalesced container refresh";
        return;
    }
    m_containersInFlight = true;
    updateLoading();
    withApiVersion(Section::Containers, [this] {
        startContainersRequest();
    });
}

void DockerBackend::startContainersRequest()
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("all"), QStringLiteral("true"));

    DockerReply *reply = m_client.get(ApiPaths::containersJson(), query);
    connect(reply, &DockerReply::finished, this, [this, reply] {
        const bool failed = reply->state() != DockerReply::State::Succeeded;
        const DockerError error = reply->error();
        const QByteArray body = reply->body();
        reply->deleteLater();

        if (failed) {
            failSection(Section::Containers, error);
            return;
        }

        QString parseError;
        int skipped = 0;
        const QList<DockerContainerDTO> dtos = DockerContainerDTO::listFromJson(body, &parseError, &skipped);
        if (!parseError.isEmpty()) {
            failSection(Section::Containers, DockerError(DockerError::Kind::UnexpectedPayload, parseError));
            return;
        }
        if (skipped > 0) {
            qCWarning(kontainerBackend) << "skipped" << skipped << "malformed container entries";
        }

        m_containers = containersFromDto(dtos);
        // The container list is the source of network membership: recompute here (idempotent)
        refreshNetworkMembership();
        m_containersInFlight = false;
        updateLoading();
        Q_EMIT containersUpdated();
    });
}

void DockerBackend::refreshImages()
{
    if (m_imagesInFlight) {
        qCDebug(kontainerBackend) << "coalesced image refresh";
        return;
    }
    m_imagesInFlight = true;
    updateLoading();
    withApiVersion(Section::Images, [this] {
        startImagesRequest();
    });
}

void DockerBackend::startImagesRequest()
{
    DockerReply *reply = m_client.get(ApiPaths::imagesJson());
    connect(reply, &DockerReply::finished, this, [this, reply] {
        const bool failed = reply->state() != DockerReply::State::Succeeded;
        const DockerError error = reply->error();
        const QByteArray body = reply->body();
        reply->deleteLater();

        if (failed) {
            failSection(Section::Images, error);
            return;
        }

        QString parseError;
        int skipped = 0;
        const QList<DockerImageDTO> dtos = DockerImageDTO::listFromJson(body, &parseError, &skipped);
        if (!parseError.isEmpty()) {
            failSection(Section::Images, DockerError(DockerError::Kind::UnexpectedPayload, parseError));
            return;
        }
        if (skipped > 0) {
            qCWarning(kontainerBackend) << "skipped" << skipped << "malformed image entries";
        }

        m_images = imagesFromDto(dtos);
        m_imagesInFlight = false;
        updateLoading();
        Q_EMIT imagesUpdated();
    });
}


void DockerBackend::refreshNetworks()
{
    if (m_networksInFlight) {
        qCDebug(kontainerBackend) << "coalesced network refresh";
        return;
    }
    m_networksInFlight = true;
    updateLoading();
    withApiVersion(Section::Networks, [this] {
        startNetworksRequest();
    });
}

void DockerBackend::startNetworksRequest()
{
    DockerReply *reply = m_client.get(ApiPaths::networks());
    connect(reply, &DockerReply::finished, this, [this, reply] {
        const bool failed = reply->state() != DockerReply::State::Succeeded;
        const DockerError error = reply->error();
        const QByteArray body = reply->body();
        reply->deleteLater();

        if (failed) {
            // Keep the previous list: networks change rarely, so a failed read must not blank the UI
            m_networksInFlight = false;
            updateLoading();
            failSection(Section::Networks, error);
            return;
        }

        QString parseError;
        int skipped = 0;
        const QList<DockerNetworkDTO> dtos = DockerNetworkDTO::listFromJson(body, &parseError, &skipped);
        if (!parseError.isEmpty()) {
            m_networksInFlight = false;
            updateLoading();
            failSection(Section::Networks, DockerError(DockerError::Kind::UnexpectedPayload, parseError));
            return;
        }
        if (skipped > 0) {
            qCWarning(kontainerBackend) << "skipped" << skipped << "malformed network entries";
        }

        m_networks = networksFromDto(dtos);
        m_networksInFlight = false;
        updateLoading();
        // Measured: the list endpoint returns an empty `Containers`, so members come from containers
        refreshNetworkMembership();
        Q_EMIT networksUpdated();
    });
}

void DockerBackend::refreshVolumes(bool includeUsage)
{
    if (m_volumesInFlight) {
        qCDebug(kontainerBackend) << "coalesced volume refresh";
        return;
    }
    m_volumesInFlight = true;
    updateLoading();
    withApiVersion(Section::Volumes, [this, includeUsage] {
        startVolumesRequest(includeUsage);
    });
}

void DockerBackend::startVolumesRequest(bool includeUsage)
{
    // `no-usage`: the engine need not scan every volume's size (slow on large hosts)
    QUrlQuery query;
    if (!includeUsage) {
        query.addQueryItem(QStringLiteral("no-usage"), QStringLiteral("1"));
    }

    DockerReply *reply = m_client.get(ApiPaths::volumes(), query);
    connect(reply, &DockerReply::finished, this, [this, reply] {
        const bool failed = reply->state() != DockerReply::State::Succeeded;
        const DockerError error = reply->error();
        const QByteArray body = reply->body();
        reply->deleteLater();

        if (failed) {
            // Same rule as networks: keep the previous list on a failed read, mark the failure only
            m_volumesInFlight = false;
            updateLoading();
            failSection(Section::Volumes, error);
            return;
        }

        QString parseError;
        QStringList warnings;
        int skipped = 0;
        const QList<DockerVolumeDTO> dtos = DockerVolumeDTO::listFromPayload(body, &warnings, &parseError, &skipped);
        if (!parseError.isEmpty()) {
            m_volumesInFlight = false;
            updateLoading();
            failSection(Section::Volumes, DockerError(DockerError::Kind::UnexpectedPayload, parseError));
            return;
        }
        for (const QString &warning : warnings) {
            // Engine warnings (e.g. a volume whose driver is unavailable) must not be dropped
            qCWarning(kontainerBackend) << "volume list warning:" << warning;
        }
        if (skipped > 0) {
            qCWarning(kontainerBackend) << "skipped" << skipped << "malformed volume entries";
        }

        m_volumes = volumesFromDto(dtos);
        m_volumesInFlight = false;
        updateLoading();
        Q_EMIT volumesUpdated();
    });
}

/* ------------------------------------------------------------------------- */
/* Phase 2: Storage / Container Detail / Image Detail / Stats                */
/* ------------------------------------------------------------------------- */

void DockerBackend::refreshStorageUsage()
{
    if (!beginRequest(QStringLiteral("storage"))) {
        return;
    }
    withApiVersion(Section::Storage, [this] {
        startStorageRequest();
    });
}

void DockerBackend::startStorageRequest()
{
    DockerReply *reply = m_client.get(ApiPaths::systemDf());
    connect(reply, &DockerReply::finished, this, [this, reply] {
        const bool failed = reply->state() != DockerReply::State::Succeeded;
        const DockerError error = reply->error();
        const QByteArray body = reply->body();
        reply->deleteLater();
        endRequest(QStringLiteral("storage"));

        if (failed) {
            failSection(Section::Storage, error);
            return;
        }

        QString parseError;
        const auto dto = DockerStorageDTO::fromPayload(body, &parseError);
        if (!dto.has_value()) {
            failSection(Section::Storage, DockerError(DockerError::Kind::UnexpectedPayload, parseError));
            return;
        }

        m_storageUsage = storageUsageFromDto(*dto);
        Q_EMIT storageUpdated();
    });
}

void DockerBackend::inspectContainer(const QString &id)
{
    if (id.isEmpty()) {
        return; // nothing to look up: send no request and report no failure
    }
    const QString key = QStringLiteral("container-inspect:") + id;
    if (!beginRequest(key)) {
        return;
    }
    withApiVersion(Section::ContainerDetail, [this, id] {
        startContainerInspectRequest(id);
    });
}

void DockerBackend::startContainerInspectRequest(const QString &id)
{
    DockerReply *reply = m_client.get(ApiPaths::containerInspect(id));
    connect(reply, &DockerReply::finished, this, [this, reply, id] {
        const bool failed = reply->state() != DockerReply::State::Succeeded;
        const DockerError error = reply->error();
        const QByteArray body = reply->body();
        reply->deleteLater();
        endRequest(QStringLiteral("container-inspect:") + id);

        if (failed) {
            failSection(Section::ContainerDetail, error);
            return;
        }

        QString parseError;
        const auto dto = DockerContainerInspectDTO::fromPayload(body, &parseError);
        if (!dto.has_value()) {
            failSection(Section::ContainerDetail, DockerError(DockerError::Kind::UnexpectedPayload, parseError));
            return;
        }

        m_containerDetail = containerDetailFromDto(*dto);
        Q_EMIT containerDetailUpdated();
    });
}

void DockerBackend::inspectImage(const QString &id)
{
    if (id.isEmpty()) {
        return;
    }
    const QString key = QStringLiteral("image-inspect:") + id;
    if (!beginRequest(key)) {
        return;
    }
    withApiVersion(Section::ImageDetail, [this, id] {
        startImageInspectRequest(id);
    });
}

void DockerBackend::startImageInspectRequest(const QString &id)
{
    DockerReply *reply = m_client.get(ApiPaths::imageInspect(id));
    connect(reply, &DockerReply::finished, this, [this, reply, id] {
        const bool failed = reply->state() != DockerReply::State::Succeeded;
        const DockerError error = reply->error();
        const QByteArray body = reply->body();
        reply->deleteLater();
        endRequest(QStringLiteral("image-inspect:") + id);

        if (failed) {
            failSection(Section::ImageDetail, error);
            return;
        }

        QString parseError;
        const auto dto = DockerImageInspectDTO::fromPayload(body, &parseError);
        if (!dto.has_value()) {
            failSection(Section::ImageDetail, DockerError(DockerError::Kind::UnexpectedPayload, parseError));
            return;
        }

        m_imageDetail = imageDetailFromDto(*dto);
        Q_EMIT imageDetailUpdated();
    });
}

void DockerBackend::requestContainerStats(const QString &id)
{
    if (id.isEmpty()) {
        return;
    }
    m_statsWanted.insert(id);
    const QString key = QStringLiteral("stats:") + id;
    if (!beginRequest(key)) {
        return; // previous sample still in flight: skip this round (§29)
    }
    withApiVersion(Section::Stats, [this, id] {
        startStatsRequest(id);
    });
}

void DockerBackend::stopContainerStats(const QString &id)
{
    // Only drops local interest: an in-flight reply is discarded on arrival (§27)
    m_statsWanted.remove(id);
}

void DockerBackend::startStatsRequest(const QString &id)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("stream"), QStringLiteral("false"));

    DockerReply *reply = m_client.get(ApiPaths::containerStats(id), query);
    connect(reply, &DockerReply::finished, this, [this, reply, id] {
        const bool failed = reply->state() != DockerReply::State::Succeeded;
        const DockerError error = reply->error();
        const QByteArray body = reply->body();
        reply->deleteLater();
        endRequest(QStringLiteral("stats:") + id);

        // Page already left: drop the result and report no error (no popup for a closed page)
        if (!m_statsWanted.contains(id)) {
            return;
        }

        if (failed) {
            failSection(Section::Stats, error);
            return;
        }

        QString parseError;
        const auto dto = DockerStatsDTO::fromPayload(body, &parseError);
        if (!dto.has_value()) {
            failSection(Section::Stats, DockerError(DockerError::Kind::UnexpectedPayload, parseError));
            return;
        }

        m_containerStats = containerStatsFromDto(*dto);
        Q_EMIT containerStatsUpdated();
    });
}

/* ============================================================================
 * Write operations (ARCH_V4 §2.2.4 / §2.3 / §2.4)
 * ==========================================================================*/

namespace
{

int mutationTimeoutMs()
{
    return int(std::chrono::duration_cast<std::chrono::milliseconds>(RefreshPolicy::kMutationTimeout).count());
}

int pullIdleTimeoutMs()
{
    return int(std::chrono::duration_cast<std::chrono::milliseconds>(RefreshPolicy::kPullIdleTimeout).count());
}

/*! Idle timeout while uploading the build context: a stalled upload of tens of MB is the anomaly. */
int buildUploadTimeoutMs()
{
    return int(std::chrono::duration_cast<std::chrono::milliseconds>(RefreshPolicy::kBuildUploadTimeout).count());
}

/*! Idle timeout for the build response phase: a build may produce no output for a long time. */
int buildIdleTimeoutMs()
{
    return int(std::chrono::duration_cast<std::chrono::milliseconds>(RefreshPolicy::kBuildIdleTimeout).count());
}

const char *mutationName(DockerBackendInterface::Mutation mutation)
{
    switch (mutation) {
    case DockerBackendInterface::Mutation::StartContainer:
        return "start-container";
    case DockerBackendInterface::Mutation::StopContainer:
        return "stop-container";
    case DockerBackendInterface::Mutation::RestartContainer:
        return "restart-container";
    case DockerBackendInterface::Mutation::PauseContainer:
        return "pause-container";
    case DockerBackendInterface::Mutation::UnpauseContainer:
        return "unpause-container";
    case DockerBackendInterface::Mutation::RemoveContainer:
        return "remove-container";
    case DockerBackendInterface::Mutation::PullImage:
        return "pull-image";
    case DockerBackendInterface::Mutation::RemoveImage:
        return "remove-image";
    case DockerBackendInterface::Mutation::CreateNetwork:
        return "create-network";
    case DockerBackendInterface::Mutation::RemoveNetwork:
        return "remove-network";
    case DockerBackendInterface::Mutation::ConnectNetwork:
        return "connect-network";
    case DockerBackendInterface::Mutation::DisconnectNetwork:
        return "disconnect-network";
    case DockerBackendInterface::Mutation::CreateContainer:
        return "create-container";
    case DockerBackendInterface::Mutation::BuildImage:
        return "build-image";
    case DockerBackendInterface::Mutation::PruneBuildCache:
        return "prune-build-cache";
    case DockerBackendInterface::Mutation::CreateVolume:
        return "create-volume";
    case DockerBackendInterface::Mutation::RemoveVolume:
        return "remove-volume";
    case DockerBackendInterface::Mutation::PruneVolumes:
        return "prune-volumes";
    }
    return "mutation";
}

bool mutationUsesDelete(DockerBackendInterface::Mutation mutation)
{
    return mutation == DockerBackendInterface::Mutation::RemoveContainer || mutation == DockerBackendInterface::Mutation::RemoveImage;
}

DockerBackendInterface::MutationOutcome outcomeFor(DockerReply *reply)
{
    using Outcome = DockerBackendInterface::MutationOutcome;
    switch (reply->state()) {
    case DockerReply::State::Succeeded:
        // 304 is the engine's "already in target state" answer to start (running) / stop (stopped)
        return reply->httpStatus() == 304 ? Outcome::Unchanged : Outcome::Succeeded;
    case DockerReply::State::Cancelled:
        return Outcome::Cancelled;
    case DockerReply::State::Failed:
    case DockerReply::State::Pending:
        return Outcome::Failed;
    }
    return Outcome::Failed;
}

} // namespace

DockerEndpoint DockerBackend::endpoint() const
{
    return m_client.endpoint();
}

void DockerBackend::emitMutationFinished(Mutation mutation, const QString &targetKey, MutationOutcome outcome, const DockerError &error)
{
    // Log only operation, target key, result: no bodies or mount paths (ARCH_V1 §27 / ARCH_V2 §40)
    const char *result = outcome == MutationOutcome::Succeeded     ? "succeeded"
        : outcome == MutationOutcome::Unchanged                    ? "unchanged"
        : outcome == MutationOutcome::Cancelled                    ? "cancelled"
                                                                   : "failed";
    if (outcome == MutationOutcome::Failed) {
        // Failures must stay diagnosable: kind + engine detail (including the HTTP status).
        // What appears here is image refs / container IDs / engine text, never credentials or mounts.
        qCWarning(kontainerBackend) << "mutation" << mutationName(mutation) << targetKey << result
                                    << "kind" << int(error.kind()) << "http" << error.httpStatus() << "detail" << error.detail();
    } else {
        qCDebug(kontainerBackend) << "mutation" << mutationName(mutation) << targetKey << result;
    }
    Q_EMIT mutationFinished(mutation, targetKey, outcome, error);
}

void DockerBackend::runMutation(Mutation mutation, const QString &targetKey, ReadyCallback run)
{
    if (m_client.hasApiVersion()) {
        run();
        return;
    }
    // Not yet handshaken: queue until version negotiation ends (writes need the prefix too)
    m_pendingMutations.append({mutation, targetKey, std::move(run)});
    if (!m_handshakeInFlight) {
        m_handshakeInFlight = true;
        m_engineInFlight = true;
        updateLoading();
        startPing();
    }
}

void DockerBackend::runContainerMutation(Mutation mutation, const QString &id, const QString &apiPath, const QUrlQuery &query)
{
    const QString targetKey = OperationTarget::container(id);
    runMutation(mutation, targetKey, [this, mutation, targetKey, apiPath, query] {
        DockerReply *reply = mutationUsesDelete(mutation) ? m_client.del(apiPath, query, mutationTimeoutMs())
                                                          : m_client.post(apiPath, query, mutationTimeoutMs());
        connect(reply, &DockerReply::finished, this, [this, reply, mutation, targetKey] {
            const DockerError error = reply->error();
            const MutationOutcome outcome = outcomeFor(reply);
            reply->deleteLater();
            emitMutationFinished(mutation, targetKey, outcome, error);
        });
    });
}

void DockerBackend::startContainer(const QString &id)
{
    runContainerMutation(Mutation::StartContainer, id, ApiPaths::containerStart(id), QUrlQuery());
}

void DockerBackend::stopContainer(const QString &id)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("t"), QString::number(RefreshPolicy::kStopTimeoutSeconds));
    runContainerMutation(Mutation::StopContainer, id, ApiPaths::containerStop(id), query);
}

void DockerBackend::restartContainer(const QString &id)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("t"), QString::number(RefreshPolicy::kStopTimeoutSeconds));
    // restart starts a stopped container outright, so there is no 304 semantics
    runContainerMutation(Mutation::RestartContainer, id, ApiPaths::containerRestart(id), query);
}

void DockerBackend::pauseContainer(const QString &id)
{
    runContainerMutation(Mutation::PauseContainer, id, ApiPaths::containerPause(id), QUrlQuery());
}

void DockerBackend::unpauseContainer(const QString &id)
{
    runContainerMutation(Mutation::UnpauseContainer, id, ApiPaths::containerUnpause(id), QUrlQuery());
}

void DockerBackend::removeContainer(const QString &id)
{
    // No v (keep anonymous and named volumes), no force (the engine must reject running ones)
    runContainerMutation(Mutation::RemoveContainer, id, ApiPaths::containerRemove(id), QUrlQuery());
}

void DockerBackend::removeImage(const QString &id, bool force)
{
    const QString targetKey = OperationTarget::image(id);
    runMutation(Mutation::RemoveImage, targetKey, [this, id, force, targetKey] {
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("force"), force ? QStringLiteral("true") : QStringLiteral("false"));

        DockerReply *reply = m_client.del(ApiPaths::imageRemove(id), query, mutationTimeoutMs());
        connect(reply, &DockerReply::finished, this, [this, reply, targetKey] {
            const DockerError error = reply->error();
            const MutationOutcome outcome = outcomeFor(reply);
            reply->deleteLater();
            emitMutationFinished(Mutation::RemoveImage, targetKey, outcome, error);
        });
    });
}

namespace
{
/*!
 * The engine reports an unreachable registry inside 5xx (DNS / connection refused / TLS /
 * proxy / timeout), separate from bad credentials (401/403), but both can arrive as 5xx.
 *
 * Heuristic: match only stable Go network-stack wording; anything else stays a plain failure,
 * so a wrong guess never calls "bad username or password" an unreachable registry.
 */
bool looksLikeUnreachableRegistry(const QString &detail)
{
    static const QStringList hints = {
        QStringLiteral("no such host"),
        QStringLiteral("dial tcp"),
        QStringLiteral("connect: connection refused"),
        QStringLiteral("i/o timeout"),
        QStringLiteral("tls handshake timeout"),
        QStringLiteral("proxyconnect"),
        QStringLiteral("certificate"),
        QStringLiteral("server misbehaving"),
        QStringLiteral("network is unreachable"),
        QStringLiteral("lookup "),
        // Measured: an unreachable registry returns 500 with context deadline exceeded /
        // request canceled while waiting for connection (timeout wording, no dial tcp)
        QStringLiteral("context deadline exceeded"),
        QStringLiteral("awaiting headers"),
        // Measured (offline): the engine returns 500 with `Get "https://…/v2/": EOF` — the
        // transport died, so this is an unreachable registry, not a malformed request
        QStringLiteral(": eof"),
        QStringLiteral("no route to host"),
        QStringLiteral("connection reset by peer"),
    };
    const QString lowered = detail.toLower();
    for (const QString &hint : hints) {
        if (lowered.contains(hint)) {
            return true;
        }
    }
    return false;
}
} // namespace

int DockerBackend::authCheckTimeoutMs() const
{
    // The engine really contacts the registry: same order of magnitude as a mutation, no long wait
    return mutationTimeoutMs();
}

void DockerBackend::checkRegistryAuth(const QString &serverAddress, const RegistryCredential &credential)
{
    const QString address = RegistryAuth::normalizeServerAddress(serverAddress.isEmpty() ? credential.serverAddress : serverAddress);
    if (address.isEmpty() || credential.isEmpty()) {
        // Incomplete arguments: skip the round trip and avoid sending half a credential
        Q_EMIT registryAuthChecked(address, AuthCheckResult::InvalidCredentials, QStringLiteral("incomplete credentials"));
        return;
    }
    if (RegistryAuth::encode(credential).isEmpty()) {
        Q_EMIT registryAuthChecked(address, AuthCheckResult::InvalidCredentials, QStringLiteral("cannot encode credentials"));
        return;
    }

    // Wait for version negotiation first: `/auth` also needs the version prefix, otherwise the
    // first check against a new backend hits an unversioned path
    withApiVersion(Section::Engine, [this, address, credential] {
        // serveraddress follows the caller's registry: the UI keeps registry and credentials in
        // separate fields, so the credential's own address is only a fallback when none is given
        RegistryCredential outgoing = credential;
        outgoing.serverAddress = address;

        // Credentials go in the body (like the docker CLI): measured, sending only an
        // `X-Registry-Auth` header with an empty body gives 400 `invalid X-Registry-Auth header:
        // invalid JSON: EOF` and every registry check fails. The body also carries the address.
        QJsonObject payload;
        payload.insert(QStringLiteral("username"), outgoing.username);
        if (!outgoing.password.isEmpty()) {
            payload.insert(QStringLiteral("password"), outgoing.password);
        }
        if (!outgoing.identityToken.isEmpty()) {
            payload.insert(QStringLiteral("identitytoken"), outgoing.identityToken);
        }
        payload.insert(QStringLiteral("serveraddress"), RegistryAuth::headerServerAddress(address));

        // Credentials stay in the body: never in the URL or logs (DockerClient logs method + path)
        DockerReply *reply = m_client.post(ApiPaths::auth(),
                                           QUrlQuery(),
                                           authCheckTimeoutMs(),
                                           {},
                                           QJsonDocument(payload).toJson(QJsonDocument::Compact));
        connect(reply, &DockerReply::finished, this, [this, reply, address] {
            const DockerError error = reply->error();
            // On failure reply->httpStatus() is 0 (only set on success); the code is in the error
            const int status = error.httpStatus() > 0 ? error.httpStatus() : reply->httpStatus();
            reply->deleteLater();

            AuthCheckResult result = AuthCheckResult::Failed;
            switch (error.kind()) {
            case DockerError::Kind::None:
                result = AuthCheckResult::Succeeded;
                break;
            case DockerError::Kind::PermissionDenied:
                // 401/403: wrong username, password or token
                result = AuthCheckResult::InvalidCredentials;
                break;
            case DockerError::Kind::Timeout:
            case DockerError::Kind::ConnectionFailed:
            case DockerError::Kind::DockerUnavailable:
                result = AuthCheckResult::RegistryUnreachable;
                break;
            default:
                result = (status >= 500 && looksLikeUnreachableRegistry(error.detail())) ? AuthCheckResult::RegistryUnreachable
                                                                                        : AuthCheckResult::Failed;
                break;
            }

            // detail is raw engine text: for logs and "technical details", never user-facing copy
            if (result != AuthCheckResult::Succeeded) {
                qCWarning(kontainerApi) << "registry auth check failed for" << address << "result" << int(result) << "status" << status;
            }
            Q_EMIT registryAuthChecked(address, result, error.detail());
        });
    });
}

int DockerBackend::logHistoryTimeoutMs() const
{
    // History logs (follow=0) use a normal timeout; follow streams end via cancel/page close (§3.1.1)
    return mutationTimeoutMs();
}

void DockerBackend::startContainerLogs(const QString &id, bool tty, bool follow, int tailLines)
{
    if (id.isEmpty()) {
        Q_EMIT containerLogsFinished(id, LogStreamEnd::Failed,
                                     DockerError(DockerError::Kind::PreconditionFailed, QStringLiteral("empty container id")));
        return;
    }

    // One stream per container: stop the old one first (it ends as Cancelled, which callers ignore)
    stopContainerLogs(id);

    // The user may have left the log section while queued for the handshake: do not open a stream
    m_cancelledLogRequests.remove(id);

    withApiVersion(Section::ContainerDetail, [this, id, tty, follow, tailLines] {
        if (m_cancelledLogRequests.remove(id)) {
            Q_EMIT containerLogsFinished(id, LogStreamEnd::Cancelled, DockerError());
            return;
        }

        QUrlQuery query;
        query.addQueryItem(QStringLiteral("stdout"), QStringLiteral("1"));
        query.addQueryItem(QStringLiteral("stderr"), QStringLiteral("1"));
        query.addQueryItem(QStringLiteral("timestamps"), QStringLiteral("0"));
        query.addQueryItem(QStringLiteral("follow"), follow ? QStringLiteral("1") : QStringLiteral("0"));
        if (tailLines > 0) {
            query.addQueryItem(QStringLiteral("tail"), QString::number(tailLines));
        }

        LogStreamState state;
        state.reader = LogFrameReader(tty);
        state.reply = m_client.getStream(ApiPaths::containerLogs(id),
                                         query,
                                         follow ? 0 : logHistoryTimeoutMs());
        const auto inserted = m_logStreams.insert(id, std::move(state));
        DockerReply *reply = inserted->reply;

        connect(reply, &DockerReply::bodyChunk, this, [this, reply, id] {
            if (reply->httpStatus() >= 400) {
                return; // 4xx/5xx handled by finished
            }
            const auto it = m_logStreams.find(id);
            if (it == m_logStreams.end()) {
                return;
            }
            const QList<LogLine> lines = it->reader.feed(reply->takeBody());
            if (!lines.isEmpty()) {
                Q_EMIT containerLogLines(id, lines);
            }
        });

        connect(reply, &DockerReply::finished, this, [this, reply, id] {
            const DockerReply::State replyState = reply->state();
            const DockerError error = reply->error();
            reply->deleteLater();

            const auto it = m_logStreams.find(id);
            if (it == m_logStreams.end() || it->reply != reply) {
                // Already settled by stopContainerLogs, or replaced by a newer stream.
                // Defensive: `DockerReply::cancel()` emits finished synchronously today, so an old
                // stream finishing after a new one cannot happen yet — but do not rely on that:
                // once cancel() is async, the old stream's teardown would wipe the new stream's state.
                return;
            }
            const bool cancelled = it->cancelled;
            const qint64 discarded = it->reader.discardedBytes();
            QList<LogLine> tail;
            if (replyState != DockerReply::State::Cancelled) {
                // Flush: the last line may lack a newline (container prompt, or a cut stream)
                tail = it->reader.flush();
            }
            m_logStreams.erase(it);

            if (discarded > 0) {
                qCWarning(kontainerBackend) << "log stream for" << id << "discarded" << discarded << "bytes of malformed data";
            }
            if (!tail.isEmpty()) {
                Q_EMIT containerLogLines(id, tail);
            }

            if (cancelled || replyState == DockerReply::State::Cancelled) {
                Q_EMIT containerLogsFinished(id, LogStreamEnd::Cancelled, DockerError());
                return;
            }
            if (replyState != DockerReply::State::Succeeded) {
                Q_EMIT containerLogsFinished(id, LogStreamEnd::Failed, error);
                return;
            }
            Q_EMIT containerLogsFinished(id, LogStreamEnd::Ended, DockerError());
        });
    });
}

void DockerBackend::stopContainerLogs(const QString &id)
{
    // Still awaiting the handshake: record it so the queued lambda gives up
    m_cancelledLogRequests.insert(id);

    const auto it = m_logStreams.find(id);
    if (it == m_logStreams.end()) {
        return; // idempotent: nothing to do when no stream is running
    }
    m_cancelledLogRequests.remove(id);
    it->cancelled = true;
    if (it->reply) {
        // finished still arrives once after cancel(); it settles there as Cancelled
        it->reply->cancel();
    }
}

void DockerBackend::buildImage(const ImageBuildRequest &request)
{
    // Target key is the build id: a resubmit is rejected instead of running two builds in parallel
    const QString targetKey = QStringLiteral("build:") + request.id;

    runMutation(Mutation::BuildImage, targetKey, [this, request, targetKey] {
        if (!QFileInfo::exists(request.contextArchive)) {
            emitMutationFinished(Mutation::BuildImage,
                                 targetKey,
                                 MutationOutcome::Failed,
                                 DockerError(DockerError::Kind::PreconditionFailed,
                                             QStringLiteral("build context archive is missing")));
            return;
        }

        QUrlQuery query;
        for (const QString &tag : request.tags) {
            query.addQueryItem(QStringLiteral("t"), tag);
        }
        if (!request.dockerfile.isEmpty() && request.dockerfile != QLatin1String("Dockerfile")) {
            query.addQueryItem(QStringLiteral("dockerfile"), request.dockerfile);
        }
        if (!request.target.isEmpty()) {
            query.addQueryItem(QStringLiteral("target"), request.target);
        }
        if (!request.platform.isEmpty()) {
            query.addQueryItem(QStringLiteral("platform"), request.platform);
        }
        if (request.noCache) {
            query.addQueryItem(QStringLiteral("nocache"), QStringLiteral("1"));
        }
        if (request.pull) {
            query.addQueryItem(QStringLiteral("pull"), QStringLiteral("1"));
        }
        if (request.removeIntermediate) {
            query.addQueryItem(QStringLiteral("rm"), QStringLiteral("1"));
        }
        if (!request.buildArgs.isEmpty()) {
            // buildargs is a JSON object whose values are strings
            QJsonObject args;
            for (const QString &entry : request.buildArgs) {
                const int separator = entry.indexOf(QLatin1Char('='));
                if (separator > 0) {
                    args.insert(entry.left(separator), entry.mid(separator + 1));
                }
            }
            query.addQueryItem(QStringLiteral("buildargs"), QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact)));
        }
        if (!request.labels.isEmpty()) {
            QJsonObject labels;
            for (const auto &label : request.labels) {
                labels.insert(label.first, label.second);
            }
            query.addQueryItem(QStringLiteral("labels"), QString::fromUtf8(QJsonDocument(labels).toJson(QJsonDocument::Compact)));
        }

        QMap<QByteArray, QByteArray> headers;
        if (!request.registryAuthHeader.isEmpty()) {
            headers.insert(QByteArrayLiteral("X-Registry-Auth"), request.registryAuthHeader);
        }

        ImageBuildState state;
        state.id = request.id;
        state.contextArchive = request.contextArchive;
        state.update.statusText = i18n("Uploading the build context…");
        m_builds.insert(request.id, state);

        // Tar upload: wide idle timeout while writing, streaming idle timeout for the response (§5.1)
        DockerReply *reply = m_client.postFile(ApiPaths::buildImage(),
                                               query,
                                               request.contextArchive,
                                               QByteArrayLiteral("application/x-tar"),
                                               buildUploadTimeoutMs(),
                                               buildIdleTimeoutMs(),
                                               headers);
        m_builds[request.id].reply = reply;

        connect(reply, &DockerReply::streamStarted, this, [this, reply, id = request.id] {
            if (reply->httpStatus() >= 400) {
                return;
            }
            if (const auto it = m_builds.constFind(id); it != m_builds.constEnd()) {
                Q_EMIT imageBuildProgress(id, it->update);
            }
        });

        connect(reply, &DockerReply::bodyChunk, this, [this, reply, id = request.id] {
            if (reply->httpStatus() >= 400) {
                return;
            }
            const auto it = m_builds.find(id);
            if (it == m_builds.end()) {
                return;
            }
            const QList<QJsonObject> lines = it->reader.feed(reply->takeBody());
            for (const QJsonObject &line : lines) {
                if (reply->isFinished() || !m_builds.contains(id)) {
                    return;
                }
                handleBuildLine(*it, line);
            }
        });

        connect(reply, &DockerReply::finished, this, [this, reply, id = request.id, targetKey] {
            const DockerReply::State state = reply->state();
            const DockerError error = reply->error();
            reply->deleteLater();

            auto it = m_builds.find(id);
            if (it == m_builds.end()) {
                return; // already cancelled and settled
            }
            it->reply = nullptr;

            if (state != DockerReply::State::Cancelled) {
                const QList<QJsonObject> tail = it->reader.finish();
                for (const QJsonObject &line : tail) {
                    handleBuildLine(*it, line);
                }
            }

            if (state == DockerReply::State::Cancelled) {
                finishBuild(id, MutationOutcome::Cancelled, DockerError());
                return;
            }
            if (state != DockerReply::State::Succeeded) {
                finishBuild(id, MutationOutcome::Failed, error);
                return;
            }
            if (it->failed) {
                // The real failure reason is the error line inside the stream (HTTP is 200 here)
                finishBuild(id, MutationOutcome::Failed, DockerError(DockerError::Kind::EngineError, it->update.errorText));
                return;
            }
            finishBuild(id, MutationOutcome::Succeeded, DockerError());
        });
    });
}

void DockerBackend::refreshNetworkMembership()
{
    if (m_networks.isEmpty()) {
        return;
    }
    bool changed = false;
    for (Network &network : m_networks) {
        QList<NetworkMember> members;
        for (const Container &container : m_containers) {
            for (const ContainerNetwork &attachment : container.networks) {
                if (attachment.id != network.id && attachment.name != network.name) {
                    continue;
                }
                NetworkMember member;
                member.containerId = container.id;
                member.name = container.name;
                member.ipv4Address = attachment.ipAddress;
                member.ipv6Address = attachment.ipv6Address;
                member.macAddress = attachment.macAddress;
                members.append(member);
                break;
            }
        }
        // Keep the network endpoint's own members (some engines fill them), deduped by container id
        for (const NetworkMember &existing : std::as_const(network.members)) {
            const bool known = std::any_of(members.cbegin(), members.cend(), [&existing](const NetworkMember &member) {
                return member.containerId == existing.containerId;
            });
            if (!known) {
                members.append(existing);
            }
        }
        if (members != network.members) {
            network.members = members;
            changed = true;
        }
    }
    if (changed) {
        Q_EMIT networksUpdated();
    }
}

void DockerBackend::abandonInFlightRequests(const DockerError &error)
{
    qCWarning(kontainerBackend) << "abandoning in-flight requests:" << error.detail();

    // Each reply's finished handler still does its own teardown; reset the flags here and fail
    // all queued callbacks together — otherwise isLoading() would stay true forever.
    m_engineInFlight = false;
    m_containersInFlight = false;
    m_imagesInFlight = false;
    m_networksInFlight = false;
    m_volumesInFlight = false;
    m_handshakeInFlight = false;
    m_client.clearApiVersion();
    m_inFlightRequests.clear();
    updateLoading();

    flushReadyCallbacks(error);
    Q_EMIT sectionFailed(Section::Engine, error);
}

void DockerBackend::pruneBuildCache()
{
    // Build cache is global: the `buildCache:` target key keeps it apart from a build's cancel
    runMutation(Mutation::PruneBuildCache, QStringLiteral("buildCache:"), [this] {
        DockerReply *reply = m_client.post(ApiPaths::buildPrune(), QUrlQuery(), mutationTimeoutMs());
        connect(reply, &DockerReply::finished, this, [this, reply] {
            const DockerReply::State state = reply->state();
            const DockerError error = reply->error();
            const QByteArray body = reply->body();
            reply->deleteLater();

            if (state != DockerReply::State::Succeeded) {
                emitMutationFinished(Mutation::PruneBuildCache,
                                     QStringLiteral("buildCache:"),
                                     outcomeFor(reply),
                                     error);
                return;
            }

            const QJsonObject object = QJsonDocument::fromJson(body).object();
            // Engine response: {"CachesDeleted":[…],"SpaceReclaimed":123}
            const qint64 reclaimed = qint64(object.value(QStringLiteral("SpaceReclaimed")).toDouble());
            Q_EMIT buildCachePruned(reclaimed);
            emitMutationFinished(Mutation::PruneBuildCache, QStringLiteral("buildCache:"), MutationOutcome::Succeeded, DockerError());
        });
    });
}

void DockerBackend::cancelImageBuild(const QString &buildId)
{
    const auto it = m_builds.constFind(buildId);
    if (it == m_builds.constEnd() || !it->reply) {
        return;
    }
    // Cancel through the reply: finished returns Cancelled, and that path deletes the temp tar
    it->reply->cancel();
}

void DockerBackend::handleBuildLine(ImageBuildState &state, const QJsonObject &object)
{
    const DockerImageBuildLineDTO line = DockerImageBuildLineDTO::fromJson(object);

    if (!line.error.isEmpty() || !line.errorDetail.isEmpty()) {
        state.failed = true;
        // The reason must name the failing step: "build failed" alone is undiagnosable (§5.3)
        const QString reason = line.errorDetail.isEmpty() ? line.error : line.errorDetail;
        state.update.errorText = state.update.stepIndex > 0 && !state.update.stepCommand.isEmpty()
            ? i18n("Step %1/%2 (%3) failed: %4",
                   state.update.stepIndex,
                   state.update.totalSteps,
                   state.update.stepCommand,
                   reason)
            : reason;
        Q_EMIT imageBuildProgress(state.id, state.update);
        return;
    }

    if (!line.auxImageId.isEmpty()) {
        state.update.auxImageId = line.auxImageId;
    }

    if (line.stepIndex > 0) {
        state.update.stepIndex = line.stepIndex;
        state.update.totalSteps = line.totalSteps;
        state.update.stepCommand = line.stepCommand;
        state.update.cached = false;
        state.update.statusText = line.stream.trimmed();
        if (line.totalSteps > 0) {
            state.update.progress = double(line.stepIndex) / double(line.totalSteps);
            state.update.progressKnown = true;
        }
        Q_EMIT imageBuildProgress(state.id, state.update);
        return;
    }

    if (line.cached) {
        state.update.cached = true;
    }
    const QString text = !line.stream.isEmpty() ? line.stream : line.status;
    if (!text.trimmed().isEmpty()) {
        state.update.statusText = text.trimmed();
        Q_EMIT imageBuildProgress(state.id, state.update);
    }
}

void DockerBackend::finishBuild(const QString &buildId, MutationOutcome outcome, const DockerError &error)
{
    auto it = m_builds.find(buildId);
    if (it == m_builds.end()) {
        return;
    }
    const QString archive = it->contextArchive;
    QString imageId;
    if (outcome == MutationOutcome::Succeeded) {
        imageId = it->update.auxImageId;
    }
    m_builds.erase(it);
    // Delete the temp tar once used (success, failure or cancel alike)
    if (!archive.isEmpty()) {
        QFile::remove(archive);
    }
    Q_EMIT imageBuildFinished(buildId, outcome, error, imageId);
    emitMutationFinished(Mutation::BuildImage, QStringLiteral("build:") + buildId, outcome, error);
}

void DockerBackend::createContainer(const ContainerCreateRequest &request)
{
    // Target key is the container name: duplicates are rejected, not created twice (engine name clash)
    const QString targetKey = OperationTarget::container(request.name);

    runMutation(Mutation::CreateContainer, targetKey, [this, request, targetKey] {
        // The name is a query parameter; the body comes from ContainerCreateRequest::toJson()
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("name"), request.name);

        DockerReply *reply = m_client.post(ApiPaths::containerCreate(),
                                           query,
                                           mutationTimeoutMs(),
                                           {},
                                           request.toJson());
        connect(reply, &DockerReply::finished, this, [this, reply, targetKey] {
            const DockerReply::State state = reply->state();
            const DockerError error = reply->error();
            const QByteArray body = reply->body();
            reply->deleteLater();

            if (state != DockerReply::State::Succeeded) {
                emitMutationFinished(Mutation::CreateContainer, targetKey, outcomeFor(reply), error);
                return;
            }

            const QJsonObject object = QJsonDocument::fromJson(body).object();
            const QString id = object.value(QStringLiteral("Id")).toString();
            const QString warning = object.value(QStringLiteral("Warnings")).toString();
            if (!warning.isEmpty()) {
                qCWarning(kontainerBackend) << "container create warning:" << warning;
            }
            if (id.isEmpty()) {
                // 201 without an id: treat as unreadable, so the UI does not report success
                emitMutationFinished(Mutation::CreateContainer,
                                     targetKey,
                                     MutationOutcome::Failed,
                                     DockerError(DockerError::Kind::InvalidResponse, QStringLiteral("create response has no Id")));
                return;
            }
            Q_EMIT containerCreated(id, warning);
            emitMutationFinished(Mutation::CreateContainer, targetKey, MutationOutcome::Succeeded, DockerError());
        });
    });
}

void DockerBackend::createVolume(const QString &name, const QString &driver, const QList<QPair<QString, QString>> &labels)
{
    const QString targetKey = OperationTarget::volume(name);

    runMutation(Mutation::CreateVolume, targetKey, [this, name, driver, labels, targetKey] {
        QJsonObject payload;
        payload.insert(QStringLiteral("Name"), name);
        if (!driver.isEmpty()) {
            payload.insert(QStringLiteral("Driver"), driver);
        }
        if (!labels.isEmpty()) {
            QJsonObject labelObject;
            for (const auto &label : labels) {
                if (!label.first.isEmpty()) {
                    labelObject.insert(label.first, label.second);
                }
            }
            payload.insert(QStringLiteral("Labels"), labelObject);
        }

        DockerReply *reply = m_client.post(ApiPaths::volumeCreate(),
                                           QUrlQuery(),
                                           mutationTimeoutMs(),
                                           {},
                                           QJsonDocument(payload).toJson(QJsonDocument::Compact));
        connect(reply, &DockerReply::finished, this, [this, reply, targetKey] {
            const DockerError error = reply->error();
            reply->deleteLater();
            emitMutationFinished(Mutation::CreateVolume, targetKey, outcomeFor(reply), error);
        });
    });
}

void DockerBackend::removeVolume(const QString &name)
{
    const QString targetKey = OperationTarget::volume(name);

    runMutation(Mutation::RemoveVolume, targetKey, [this, name, targetKey] {
        // No force: let the engine refuse a volume still in use, and show its reason
        DockerReply *reply = m_client.del(ApiPaths::volume(name), QUrlQuery(), mutationTimeoutMs());
        connect(reply, &DockerReply::finished, this, [this, reply, targetKey] {
            const DockerError error = reply->error();
            reply->deleteLater();
            emitMutationFinished(Mutation::RemoveVolume, targetKey, outcomeFor(reply), error);
        });
    });
}

void DockerBackend::pruneVolumes()
{
    const QString targetKey = OperationTarget::volumePrune();

    runMutation(Mutation::PruneVolumes, targetKey, [this, targetKey] {
        DockerReply *reply = m_client.post(ApiPaths::volumesPrune(), QUrlQuery(), mutationTimeoutMs());
        connect(reply, &DockerReply::finished, this, [this, reply, targetKey] {
            const DockerReply::State state = reply->state();
            const DockerError error = reply->error();
            const QByteArray body = reply->body();
            reply->deleteLater();

            if (state != DockerReply::State::Succeeded) {
                emitMutationFinished(Mutation::PruneVolumes, targetKey, outcomeFor(reply), error);
                return;
            }

            // Response carries what was deleted and how much was reclaimed; sent via volumesPruned
            const QJsonObject object = QJsonDocument::fromJson(body).object();
            QStringList names;
            const QJsonArray deleted = object.value(QStringLiteral("VolumesDeleted")).toArray();
            for (const QJsonValue &entry : deleted) {
                if (entry.isString()) {
                    names.append(entry.toString());
                }
            }
            const qint64 reclaimed = qint64(object.value(QStringLiteral("SpaceReclaimed")).toDouble(0));
            Q_EMIT volumesPruned(names, reclaimed);
            emitMutationFinished(Mutation::PruneVolumes, targetKey, MutationOutcome::Succeeded, DockerError());
        });
    });
}

void DockerBackend::createNetwork(const NetworkCreateRequest &request)
{
    // Target key is the network name: a duplicate submit is rejected, not created twice
    const QString targetKey = OperationTarget::network(request.name);

    runMutation(Mutation::CreateNetwork, targetKey, [this, request, targetKey] {
        // Creation uses a JSON body (other writes use query parameters): see DockerClient::post
        DockerReply *reply = m_client.post(ApiPaths::networkCreate(), QUrlQuery(), mutationTimeoutMs(), {}, request.toJson());
        connect(reply, &DockerReply::finished, this, [this, reply, targetKey] {
            const DockerReply::State state = reply->state();
            const DockerError error = reply->error();
            const QByteArray body = reply->body();
            reply->deleteLater();

            if (state != DockerReply::State::Succeeded) {
                emitMutationFinished(Mutation::CreateNetwork, targetKey, outcomeFor(reply), error);
                return;
            }

            // A 201 may carry a Warning (e.g. "this name will be truncated"): log it and pass it on
            const QJsonObject object = QJsonDocument::fromJson(body).object();
            const QString warning = object.value(QStringLiteral("Warning")).toString();
            if (!warning.isEmpty()) {
                qCWarning(kontainerBackend) << "network create warning:" << warning;
                emitMutationFinished(Mutation::CreateNetwork,
                                     targetKey,
                                     MutationOutcome::Succeeded,
                                     DockerError(DockerError::Kind::None, warning));
                return;
            }
            emitMutationFinished(Mutation::CreateNetwork, targetKey, MutationOutcome::Succeeded, DockerError());
        });
    });
}

void DockerBackend::removeNetwork(const QString &id)
{
    const QString targetKey = OperationTarget::network(id);

    runMutation(Mutation::RemoveNetwork, targetKey, [this, id, targetKey] {
        DockerReply *reply = m_client.del(ApiPaths::network(id), QUrlQuery(), mutationTimeoutMs());
        connect(reply, &DockerReply::finished, this, [this, reply, targetKey] {
            const DockerError error = reply->error();
            reply->deleteLater();
            emitMutationFinished(Mutation::RemoveNetwork, targetKey, outcomeFor(reply), error);
        });
    });
}

void DockerBackend::connectNetwork(const QString &networkId, const QString &containerId, const QStringList &aliases)
{
    // Target key is network + container: different containers may connect concurrently
    const QString targetKey = OperationTarget::network(networkId) + QLatin1Char('/') + containerId;

    runMutation(Mutation::ConnectNetwork, targetKey, [this, networkId, containerId, aliases, targetKey] {
        QJsonObject endpoint;
        if (!aliases.isEmpty()) {
            QJsonArray aliasArray;
            for (const QString &alias : aliases) {
                const QString trimmed = alias.trimmed();
                if (!trimmed.isEmpty()) {
                    aliasArray.append(trimmed);
                }
            }
            endpoint.insert(QStringLiteral("Aliases"), aliasArray);
        }
        QJsonObject payload;
        payload.insert(QStringLiteral("Container"), containerId);
        if (!endpoint.isEmpty()) {
            payload.insert(QStringLiteral("EndpointConfig"), endpoint);
        }

        DockerReply *reply = m_client.post(ApiPaths::networkConnect(networkId),
                                           QUrlQuery(),
                                           mutationTimeoutMs(),
                                           {},
                                           QJsonDocument(payload).toJson(QJsonDocument::Compact));
        connect(reply, &DockerReply::finished, this, [this, reply, targetKey] {
            const DockerError error = reply->error();
            reply->deleteLater();
            emitMutationFinished(Mutation::ConnectNetwork, targetKey, outcomeFor(reply), error);
        });
    });
}

void DockerBackend::disconnectNetwork(const QString &networkId, const QString &containerId, bool force)
{
    const QString targetKey = OperationTarget::network(networkId) + QLatin1Char('/') + containerId;

    runMutation(Mutation::DisconnectNetwork, targetKey, [this, networkId, containerId, force, targetKey] {
        QJsonObject payload;
        payload.insert(QStringLiteral("Container"), containerId);
        payload.insert(QStringLiteral("Force"), force);

        DockerReply *reply = m_client.post(ApiPaths::networkDisconnect(networkId),
                                           QUrlQuery(),
                                           mutationTimeoutMs(),
                                           {},
                                           QJsonDocument(payload).toJson(QJsonDocument::Compact));
        connect(reply, &DockerReply::finished, this, [this, reply, targetKey] {
            const DockerError error = reply->error();
            reply->deleteLater();
            emitMutationFinished(Mutation::DisconnectNetwork, targetKey, outcomeFor(reply), error);
        });
    });
}

void DockerBackend::pullImage(const QString &reference, const RegistryCredential &credential)
{
    const QString targetKey = OperationTarget::image(ImageReference::normalized(reference));

    // A duplicate pull of the same reference is rejected with a clear message (others run in parallel)
    if (m_pulls.contains(targetKey)) {
        emitMutationFinished(Mutation::PullImage,
                             targetKey,
                             MutationOutcome::Failed,
                             DockerError(DockerError::Kind::PreconditionFailed, QStringLiteral("this image is already being pulled")));
        return;
    }

    if (!ImageReference::isValid(reference)) {
        emitMutationFinished(Mutation::PullImage,
                             targetKey,
                             MutationOutcome::Failed,
                             DockerError(DockerError::Kind::PreconditionFailed, QStringLiteral("invalid image reference")));
        return;
    }

    runMutation(Mutation::PullImage, targetKey, [this, reference, targetKey, credential] {
        startPullRequest(reference, targetKey, credential);
    });
}

void DockerBackend::startPullRequest(const QString &reference, const QString &targetKey, const RegistryCredential &credential)
{
    const auto parts = ImageReference::parse(reference);
    if (!parts) {
        emitMutationFinished(Mutation::PullImage,
                             targetKey,
                             MutationOutcome::Failed,
                             DockerError(DockerError::Kind::PreconditionFailed, QStringLiteral("invalid image reference")));
        return;
    }

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("fromImage"), parts->fromImage());
    if (!parts->tag.isEmpty()) {
        query.addQueryItem(QStringLiteral("tag"), parts->tag);
    }

    // Note: a QHash reference can be invalidated by the insert, so insert first and use the
    // returned iterator; every later callback re-looks-up state by targetKey instead of holding one.
    ImagePullState state;
    state.reference = ImageReference::normalized(reference);
    state.targetKey = targetKey;
    state.progress.reference = state.reference;
    const auto inserted = m_pulls.insert(targetKey, state);
    // Credentials go only in a header (none when empty, keeping anonymous pulls unchanged);
    // serveraddress uses the image's own registry, never some other registry the caller passed.
    QMap<QByteArray, QByteArray> headers;
    if (!credential.isEmpty()) {
        RegistryCredential outgoing = credential;
        const QString registry = RegistryAuth::serverAddressForImage(reference);
        if (!registry.isEmpty()) {
            outgoing.serverAddress = registry;
        }
        const QByteArray encoded = RegistryAuth::encode(outgoing);
        if (!encoded.isEmpty()) {
            headers.insert(QByteArrayLiteral("X-Registry-Auth"), encoded);
        }
    }
    inserted->reply = m_client.postStream(ApiPaths::imageCreate(), query, pullIdleTimeoutMs(), headers);
    DockerReply *reply = inserted->reply;

    connect(reply, &DockerReply::streamStarted, this, [this, reply, targetKey] {
        if (reply->httpStatus() >= 400) {
            return; // 4xx/5xx handled by finished
        }
        if (const auto it = m_pulls.constFind(targetKey); it != m_pulls.constEnd()) {
            emitPullProgress(*it);
        }
    });

    connect(reply, &DockerReply::bodyChunk, this, [this, reply, targetKey] {
        if (reply->httpStatus() >= 400) {
            return;
        }
        const auto it = m_pulls.find(targetKey);
        if (it == m_pulls.end()) {
            return;
        }
        const QList<QJsonObject> lines = it->reader.feed(reply->takeBody());
        for (const QJsonObject &line : lines) {
            // A subscriber may cancel synchronously inside imagePullProgress: stop writing progress then
            if (reply->isFinished() || !m_pulls.contains(targetKey)) {
                return;
            }
            handlePullLine(*it, line);
        }
    });

    connect(reply, &DockerReply::finished, this, [this, reply, targetKey] {
        const DockerReply::State state = reply->state();
        const DockerError error = reply->error();
        reply->deleteLater();

        auto it = m_pulls.find(targetKey);
        if (it == m_pulls.end()) {
            return; // already cancelled and settled
        }
        it->reply = nullptr;

        if (state != DockerReply::State::Cancelled) {
            // Flush: the last line may lack a newline
            const QList<QJsonObject> tail = it->reader.finish();
            for (const QJsonObject &line : tail) {
                handlePullLine(*it, line);
            }
        }

        if (it->reader.malformedLines() > 0 || it->reader.droppedLines() > 0) {
            qCWarning(kontainerBackend) << "image pull stream had" << it->reader.malformedLines() << "malformed and"
                                        << it->reader.droppedLines() << "dropped lines";
        }

        if (state == DockerReply::State::Cancelled) {
            finishPull(targetKey, MutationOutcome::Cancelled, DockerError());
            return;
        }
        if (state != DockerReply::State::Succeeded) {
            finishPull(targetKey, MutationOutcome::Failed, error);
            return;
        }
        if (it->failed) {
            // The real failure reason is the error line inside the stream (HTTP status is 200 here)
            const QString message = it->progress.errorText;
            finishPull(targetKey, MutationOutcome::Failed, DockerError(DockerError::Kind::EngineError, message));
            return;
        }

        it->progress.phase = ImagePullProgress::Phase::Complete;
        if (it->progress.totalBytes > 0) {
            it->progress.currentBytes = it->progress.totalBytes;
        }
        emitPullProgress(*it);
        finishPull(targetKey, MutationOutcome::Succeeded, DockerError());
    });
}

void DockerBackend::finishPull(const QString &targetKey, MutationOutcome outcome, const DockerError &error)
{
    m_pulls.remove(targetKey);
    emitMutationFinished(Mutation::PullImage, targetKey, outcome, error);
}

void DockerBackend::cancelImagePull(const QString &reference)
{
    const QString targetKey = OperationTarget::image(ImageReference::normalized(reference));
    const auto it = m_pulls.find(targetKey);
    if (it == m_pulls.end()) {
        return;
    }
    qCDebug(kontainerBackend) << "cancelling image pull" << targetKey;
    // finished (state = Cancelled) arrives right after, and settles the pull there
    if (it->reply) {
        it->reply->cancel();
    } else {
        finishPull(targetKey, MutationOutcome::Cancelled, DockerError());
    }
}

void DockerBackend::cancelAllImagePulls()
{
    const QList<QString> keys = m_pulls.keys();
    for (const QString &targetKey : keys) {
        const auto it = m_pulls.find(targetKey);
        if (it != m_pulls.end() && it->reply) {
            it->reply->cancel();
        }
    }
}

void DockerBackend::emitPullProgress(const ImagePullState &state)
{
    Q_EMIT imagePullProgress(state.progress);
}

void DockerBackend::handlePullLine(ImagePullState &state, const QJsonObject &object)
{
    const DockerImagePullLineDTO line = DockerImagePullLineDTO::fromJson(object);

    if (!line.error.isEmpty()) {
        state.failed = true;
        state.progress.phase = ImagePullProgress::Phase::Failed;
        state.progress.errorText = line.error;
        emitPullProgress(state);
        return;
    }

    if (!line.status.isEmpty()) {
        // status is raw engine text ("Downloading", "Pull complete", …): shown as data, not translated
        state.progress.statusText = line.status;
        const ImagePullProgress::Phase phase = DockerImagePullLineDTO::phaseForStatus(line.status);
        if (phase != ImagePullProgress::Phase::Waiting) {
            state.progress.phase = phase;
        }
    }
    if (!line.id.isEmpty()) {
        state.progress.layerId = line.id;
        // Lines like "Pulling from <repo>" also carry an id (a tag, not a layer): do not count them
        if (line.isLayerStatus()) {
            PullLayerState &layer = state.layers[line.id];
            if (line.hasProgress) {
                layer.current = line.current;
                layer.total = line.total;
            }
            if (line.layerFinished()) {
                layer.complete = true;
                if (layer.total > 0) {
                    layer.current = layer.total;
                }
            }
        }
    }

    updatePullTotals(state);
    emitPullProgress(state);
}

void DockerBackend::updatePullTotals(ImagePullState &state)
{
    qint64 current = 0;
    qint64 total = 0;
    int completed = 0;
    for (auto it = state.layers.constBegin(); it != state.layers.constEnd(); ++it) {
        current += it->current;
        total += it->total;
        if (it->complete) {
            ++completed;
        }
    }
    state.progress.currentBytes = current;
    state.progress.totalBytes = total;
    state.progress.completedLayers = completed;
    state.progress.totalLayers = state.layers.size();
}

} // namespace Kontainer
