/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/docker_backend.h"

#include "backend/docker_api_paths.h"

#include <QJsonDocument>
#include <QJsonObject>

#include "dto/container_dto.h"
#include "dto/container_inspect_dto.h"
#include "dto/image_dto.h"
#include "dto/network_dto.h"
#include "dto/image_inspect_dto.h"
#include "dto/image_pull_dto.h"
#include "dto/stats_dto.h"
#include "dto/storage_dto.h"
#include "domain/image_reference.h"
#include "logging.h"
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
        m_engine.securityOptions = info->securityOptions;
        m_engine.dockerRootDir = info->dockerRootDir;
        m_engine.loggingDriver = info->loggingDriver;
        m_engine.registryMirrors = info->registryMirrors;
        m_engine.liveRestoreEnabled = info->liveRestoreEnabled;
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

    // 等待握手的写操作：成功则执行，失败则作为 mutation 失败上报
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
            // 保留上一次的列表：网络是低频数据，"读失败"不该让界面突然空掉
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
        Q_EMIT networksUpdated();
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

    DockerReply *reply = m_client.get(ApiPaths::containerStats(id), query);
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

/* ============================================================================
 * 写操作（ARCH_V4 §2.2.4 / §2.3 / §2.4）
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

const char *mutationName(DockerBackendInterface::Mutation mutation)
{
    switch (mutation) {
    case DockerBackendInterface::Mutation::StartContainer:
        return "start-container";
    case DockerBackendInterface::Mutation::StopContainer:
        return "stop-container";
    case DockerBackendInterface::Mutation::RestartContainer:
        return "restart-container";
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
        // 304 是引擎对 start（已运行）/ stop（已停止）的「已处于目标状态」语义
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
    // 只记录操作、目标 key 与结果：不记录请求体、响应体或挂载路径（ARCH_V1 §27 / ARCH_V2 §40）
    const char *result = outcome == MutationOutcome::Succeeded     ? "succeeded"
        : outcome == MutationOutcome::Unchanged                    ? "unchanged"
        : outcome == MutationOutcome::Cancelled                    ? "cancelled"
                                                                   : "failed";
    if (outcome == MutationOutcome::Failed) {
        // 失败必须可诊断：分类 + 引擎原文（HTTP 状态码也在里面）。
        // 这里出现的是镜像引用 / 容器 ID / 引擎消息，不含凭据与挂载路径。
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
    // 还没握手：排队等版本协商结束（写请求同样需要版本前缀）
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
    // restart 对已停止的容器会直接启动，因此没有 304 语义
    runContainerMutation(Mutation::RestartContainer, id, ApiPaths::containerRestart(id), query);
}

void DockerBackend::removeContainer(const QString &id)
{
    // 不带 v（保留匿名卷与命名卷）、不带 force（运行中的容器必须由引擎拒绝）
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
 * 引擎把"联系不上仓库"包在 5xx 里返回（DNS / 连接被拒 / TLS / 代理 / 超时），
 * 与"凭据不对"（401/403）在状态码上是分开的，但都可能是 5xx。
 *
 * 这是**启发式**：只认引擎（Go）网络栈的稳定措辞，认不出来就归到普通失败，
 * 不会因为猜错而把"用户名密码错误"说成"仓库不可达"。
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
    // 引擎要真的去联系仓库：与写操作同一个量级，避免用户干等
    return mutationTimeoutMs();
}

void DockerBackend::checkRegistryAuth(const QString &serverAddress, const RegistryCredential &credential)
{
    const QString address = RegistryAuth::normalizeServerAddress(serverAddress.isEmpty() ? credential.serverAddress : serverAddress);
    if (address.isEmpty() || credential.isEmpty()) {
        // 参数不全就不发请求：既省一次往返，也避免把半个凭据发出去
        Q_EMIT registryAuthChecked(address, AuthCheckResult::InvalidCredentials, QStringLiteral("incomplete credentials"));
        return;
    }
    if (RegistryAuth::encode(credential).isEmpty()) {
        Q_EMIT registryAuthChecked(address, AuthCheckResult::InvalidCredentials, QStringLiteral("cannot encode credentials"));
        return;
    }

    // 没握手时先等版本协商：`/auth` 同样需要版本前缀，否则新后端上的第一次校验会打到不带版本的路径
    withApiVersion(Section::Engine, [this, address, credential] {
        // 头里的 serveraddress 以调用方给的仓库为准：界面上的"仓库 + 用户名密码"是两个字段，
        // 凭据结构里的同名字段只在参数为空时兜底，避免两者不一致时把凭据发给错误的仓库
        RegistryCredential outgoing = credential;
        outgoing.serverAddress = address;
        QMap<QByteArray, QByteArray> headers;
        headers.insert(QByteArrayLiteral("X-Registry-Auth"), RegistryAuth::encode(outgoing));

        // 凭据只在请求头里：query 与请求体都不带它（不进日志、不进 URL）
        DockerReply *reply = m_client.post(ApiPaths::auth(), QUrlQuery(), authCheckTimeoutMs(), headers);
        connect(reply, &DockerReply::finished, this, [this, reply, address] {
            const DockerError error = reply->error();
            // 失败时 reply->httpStatus() 是 0（它只在成功路径上被赋值），状态码在错误对象里
            const int status = error.httpStatus() > 0 ? error.httpStatus() : reply->httpStatus();
            reply->deleteLater();

            AuthCheckResult result = AuthCheckResult::Failed;
            switch (error.kind()) {
            case DockerError::Kind::None:
                result = AuthCheckResult::Succeeded;
                break;
            case DockerError::Kind::PermissionDenied:
                // 401/403：用户名、密码或令牌不对
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

            // detail 是引擎原文：只用于日志与"技术细节"，不当作用户文案
            if (result != AuthCheckResult::Succeeded) {
                qCWarning(kontainerApi) << "registry auth check failed for" << address << "result" << int(result) << "status" << status;
            }
            Q_EMIT registryAuthChecked(address, result, error.detail());
        });
    });
}

int DockerBackend::logHistoryTimeoutMs() const
{
    // 历史日志（follow=0）用普通超时；follow 流交给取消与页面生命周期结束（§3.1.1）
    return mutationTimeoutMs();
}

void DockerBackend::startContainerLogs(const QString &id, bool tty, bool follow, int tailLines)
{
    if (id.isEmpty()) {
        Q_EMIT containerLogsFinished(id, LogStreamEnd::Failed,
                                     DockerError(DockerError::Kind::PreconditionFailed, QStringLiteral("empty container id")));
        return;
    }

    // 一个容器最多一路流：重连/换容器时先停掉旧的（旧流会以 Cancelled 结束，调用方据此忽略）
    stopContainerLogs(id);

    // 排队等握手时用户可能已经离开日志分区：那时不该再去开流
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
                return; // 4xx/5xx 交给 finished 统一处理
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
                // 已经被 stopContainerLogs 清算，或者已被新的流取代。
                // 这一条是**防御性**的：`DockerReply::cancel()` 目前同步发 finished，
                // 所以"旧流晚于新流收尾"的顺序今天构造不出来——但不要依赖这个实现细节，
                // 一旦 cancel() 改成异步，旧流的收尾会把新流的状态擦掉。
                return;
            }
            const bool cancelled = it->cancelled;
            const qint64 discarded = it->reader.discardedBytes();
            QList<LogLine> tail;
            if (replyState != DockerReply::State::Cancelled) {
                // 收尾：最后一行可能没有换行（容器输出提示符、或流被切断）
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
    // 还在等版本握手的情况：记下来，让排队的 lambda 自己放弃
    m_cancelledLogRequests.insert(id);

    const auto it = m_logStreams.find(id);
    if (it == m_logStreams.end()) {
        return; // 幂等：没有在跑的流什么都不做
    }
    m_cancelledLogRequests.remove(id);
    it->cancelled = true;
    if (it->reply) {
        // cancel() 之后 finished 仍会来一次，届时按 Cancelled 收尾
        it->reply->cancel();
    }
}

void DockerBackend::createNetwork(const NetworkCreateRequest &request)
{
    // 目标键用网络名：同一个名字重复提交会被拒绝，而不是并发建出两个
    const QString targetKey = OperationTarget::network(request.name);

    runMutation(Mutation::CreateNetwork, targetKey, [this, request, targetKey] {
        // 创建是 JSON 体（四期的写操作都靠 query）：见 DockerClient::post 的 body 参数
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

            // 引擎可以在 201 里带一条 Warning（例如"这个名字会被截断"）：写进日志并透出给界面
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

void DockerBackend::pullImage(const QString &reference, const RegistryCredential &credential)
{
    const QString targetKey = OperationTarget::image(ImageReference::normalized(reference));

    // 同一个引用重复拉取：拒绝并给出明确文案（不同引用可以并发）
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

    // 注意：QHash 的引用在插入时可能失效，因此这里先插入、再用迭代器访问，
    // 并且后续所有回调都通过 targetKey 重新查找状态，不保存引用。
    ImagePullState state;
    state.reference = ImageReference::normalized(reference);
    state.targetKey = targetKey;
    state.progress.reference = state.reference;
    const auto inserted = m_pulls.insert(targetKey, state);
    // 凭据只走请求头（空凭据不加头，保持匿名拉取的原样）；serveraddress 用镜像所在的仓库，
    // 避免调用方给的凭据结构里写着别的仓库
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
            return; // 4xx/5xx 交给 finished 统一处理
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
            // 订阅者可能在 imagePullProgress 里同步取消：取消后不要再写进度
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
            return; // 已经被取消并清算过
        }
        it->reply = nullptr;

        if (state != DockerReply::State::Cancelled) {
            // 收尾：最后一行可能没有换行符
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
            // 流内的 error 行才是真正的失败原因（此时 HTTP 状态是 200）
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
    // 取消后紧接着会收到 finished（state = Cancelled），由那里统一清算
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
        // status 是引擎原文（"Downloading"、"Pull complete"…），按数据显示，不翻译
        state.progress.statusText = line.status;
        const ImagePullProgress::Phase phase = DockerImagePullLineDTO::phaseForStatus(line.status);
        if (phase != ImagePullProgress::Phase::Waiting) {
            state.progress.phase = phase;
        }
    }
    if (!line.id.isEmpty()) {
        state.progress.layerId = line.id;
        // "Pulling from <repo>" 这类行也带 id（那是 tag，不是层），不能算进层数
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
