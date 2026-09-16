/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/docker_backend_interface.h"
#include "backend/docker_client.h"
#include "domain/container.h"
#include "domain/container_detail.h"
#include "domain/container_stats.h"
#include "domain/engine_info.h"
#include "domain/image.h"
#include "domain/image_detail.h"
#include "domain/storage_usage.h"
#include "dto/engine_dto.h"

#include <QList>
#include <QSet>

#include <functional>

namespace Kontainer
{

/*!
 * 真实的只读 Docker backend（ARCH_V1 §6.3/§11/§15/§17，ARCH_V2 §29/§42）。
 *
 * 职责：HTTP 请求、Unix socket、HTTP 状态码、JSON 解码、API 版本处理、
 * 错误映射、请求去重。不含任何 UI 文本，也不知道用户当前在哪个页面（§43）。
 *
 * 只读约束：本类只会发起 GET（/_ping、/version、/info、/containers/json、
 * /images/json、/system/df、/containers/{id}/json、/images/{id}/json、
 * /containers/{id}/stats?stream=false），没有 POST/PUT/PATCH/DELETE。
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

    void refreshStorageUsage() override;
    void inspectContainer(const QString &id) override;
    void inspectImage(const QString &id) override;
    void requestContainerStats(const QString &id) override;
    void stopContainerStats(const QString &id) override;

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

    /*! 需要 API 版本前缀的请求：若尚未协商，则先完成握手再执行。 */
    void withApiVersion(Section section, ReadyCallback callback);

    void startPing();
    void startVersionRequest();
    void startInfoRequest();
    void startContainersRequest();
    void startImagesRequest();
    void startStorageRequest();
    void startContainerInspectRequest(const QString &id);
    void startImageInspectRequest(const QString &id);
    void startStatsRequest(const QString &id);

    void finishHandshakeSuccess();
    void finishHandshakeFailure(const DockerError &error);
    /*! 清空 /info 汇总计数，避免把上一次的旧计数当成当前值展示。 */
    void resetEngineCounts();
    void flushReadyCallbacks(const DockerError &error);
    void failSection(Section section, const DockerError &error);
    void updateLoading();

    /*!
     * 请求去重（ARCH_V2 §29）：key = 请求类型 + 资源。
     * 返回 false 表示同一资源已有同类请求在途，调用方应合并（coalesce）本次请求。
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
    bool m_handshakeInFlight = false;
    bool m_loading = false;

    /*! 在途请求 key 集合（inspect / stats / storage 的去重）。 */
    QSet<QString> m_inFlightRequests;
    /*! 仍然需要 stats 的容器（离开详情页后移除，§27）。 */
    QSet<QString> m_statsWanted;

    QList<QPair<Section, ReadyCallback>> m_readyCallbacks;
};

} // namespace Kontainer
