/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/docker_backend.h"

#include "dto/container_dto.h"
#include "dto/container_inspect_dto.h"
#include "dto/image_dto.h"
#include "dto/image_inspect_dto.h"
#include "dto/stats_dto.h"
#include "dto/storage_dto.h"
#include "logging.h"

#include <QUrlQuery>

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
        // 同一资源同类请求已在途：合并本次请求（ARCH_V2 §29 coalescing）
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
/* Engine：/_ping → /version（协商 API 版本）→ /info                          */
/* ------------------------------------------------------------------------- */

void DockerBackend::refreshEngine()
{
    if (m_engineInFlight) {
        // 请求去重：同一数据集不重复创建请求（ARCH_V1 §17）
        qCDebug(kontainerBackend) << "coalesced engine refresh";
        return;
    }
    m_engineInFlight = true;
    updateLoading();

    if (m_handshakeInFlight) {
        return; // 已有握手在途（可能由 containers/images 触发）
    }
    m_handshakeInFlight = true;
    startPing();
}

void DockerBackend::startPing()
{
    DockerReply *reply = m_client.getUnversioned(QStringLiteral("/_ping"));
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
    DockerReply *reply = m_client.getUnversioned(QStringLiteral("/version"));
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

        startInfoRequest();
    });
}

void DockerBackend::startInfoRequest()
{
    DockerReply *reply = m_client.get(QStringLiteral("/info"));
    connect(reply, &DockerReply::finished, this, [this, reply] {
        const bool failed = reply->state() != DockerReply::State::Succeeded;
        const DockerError error = reply->error();
        const QByteArray body = reply->body();
        reply->deleteLater();

        if (failed) {
            // 错误隔离（§15）：版本信息已经可用，容器/镜像仍然可以继续加载。
            // 但 /info 的汇总计数必须作废，否则 UI 会把上一次的旧计数当成当前值。
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
        m_engine.containerTotal = info->containers;
        m_engine.containersRunning = info->containersRunning;
        m_engine.containersPaused = info->containersPaused;
        m_engine.containersStopped = info->containersStopped;
        m_engine.imageCount = info->images;
        m_engine.memoryTotalBytes = info->memoryTotalBytes;
        // /info 的信息比 /version 更完整时优先采用
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

    // 依赖握手的数据集一并失败，但各自独立上报（错误隔离）
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
    case Section::Storage:
    case Section::ContainerDetail:
    case Section::ImageDetail:
    case Section::Stats:
        // 这些分区的在途状态由 m_inFlightRequests 管理
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
        m_engineInFlight = true; // 握手同时会填充 Engine 状态
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

    DockerReply *reply = m_client.get(QStringLiteral("/containers/json"), query);
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
    DockerReply *reply = m_client.get(QStringLiteral("/images/json"));
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


/* ------------------------------------------------------------------------- */
/* 二期：Storage / Container Detail / Image Detail / Stats                     */
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
    DockerReply *reply = m_client.get(QStringLiteral("/system/df"));
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
        return; // 没有资源可查：不发请求，也不算失败
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
    DockerReply *reply = m_client.get(QStringLiteral("/containers/%1/json").arg(id));
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
    DockerReply *reply = m_client.get(QStringLiteral("/images/%1/json").arg(id));
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
        return; // 上一次采样还没回来：本轮跳过（§29）
    }
    withApiVersion(Section::Stats, [this, id] {
        startStatsRequest(id);
    });
}

void DockerBackend::stopContainerStats(const QString &id)
{
    // 只结束本地采样兴趣：在途请求返回后会被丢弃（§27）
    m_statsWanted.remove(id);
}

void DockerBackend::startStatsRequest(const QString &id)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("stream"), QStringLiteral("false"));

    DockerReply *reply = m_client.get(QStringLiteral("/containers/%1/stats").arg(id), query);
    connect(reply, &DockerReply::finished, this, [this, reply, id] {
        const bool failed = reply->state() != DockerReply::State::Succeeded;
        const DockerError error = reply->error();
        const QByteArray body = reply->body();
        reply->deleteLater();
        endRequest(QStringLiteral("stats:") + id);

        // 页面已经离开：丢弃结果，也不上报错误（避免给已关闭的页面弹错误）
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

} // namespace Kontainer
