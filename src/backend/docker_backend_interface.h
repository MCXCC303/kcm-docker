/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/docker_error.h"
#include "domain/container.h"
#include "domain/container_detail.h"
#include "domain/container_stats.h"
#include "domain/engine_info.h"
#include "domain/image.h"
#include "domain/image_detail.h"
#include "domain/storage_usage.h"

#include <QList>
#include <QObject>
#include <QString>

namespace Kontainer
{

/*!
 * Backend 抽象（ARCH_V1 §30 / ARCH_V2 §42）。
 *
 * 真实实现 DockerBackend 访问 Docker Engine；测试实现 MockDockerBackend
 * （tests/support/）提供可控数据，使 model / UI 不依赖真实 daemon 即可测试。
 *
 * API 表达“我要读取什么”，而不是“我要发送什么 HTTP 请求”（ARCH_V1 §11）。
 * 二期接口全部只读，且 Backend 不知道任何页面/导航信息（ARCH_V2 §43）。
 */
class DockerBackendInterface : public QObject
{
    Q_OBJECT

public:
    /*! 分区（ARCH_V2 §30）：任一数据集的失败只影响该分区。 */
    enum class Section {
        Engine,
        Containers,
        Images,
        Storage,
        ContainerDetail,
        ImageDetail,
        Stats,
    };
    Q_ENUM(Section)

    explicit DockerBackendInterface(QObject *parent = nullptr);
    ~DockerBackendInterface() override;

    /* --- 高频数据集 --- */
    virtual void refreshEngine() = 0;
    virtual void refreshContainers() = 0;
    virtual void refreshImages() = 0;
    /*! 高频数据集一起刷新（Engine + Containers + Images）。 */
    virtual void refreshAll();

    /* --- 中频 / 按需数据集 --- */
    /*! Engine 级磁盘占用（`GET /system/df`）。 */
    virtual void refreshStorageUsage() = 0;
    /*! 容器 inspect（`GET /containers/{id}/json`）。 */
    virtual void inspectContainer(const QString &id) = 0;
    /*! 镜像 inspect（`GET /images/{id}/json`）。 */
    virtual void inspectImage(const QString &id) = 0;
    /*! 单次容器 stats 采样（`GET /containers/{id}/stats?stream=false`）。 */
    virtual void requestContainerStats(const QString &id) = 0;
    /*!
     * 停止对某个容器的 stats 采样兴趣（ARCH_V2 §27：离开详情页必须停止）。
     * 这只结束本地请求生命周期，不属于 Docker mutation。
     */
    virtual void stopContainerStats(const QString &id) = 0;

    /*! 是否有请求在途。 */
    virtual bool isLoading() const = 0;
    /*!
     * 高频数据集（Engine/Containers/Images）是否有请求在途。
     * 自动刷新只应被这些请求阻塞，不应被详情/采样请求拖住（ARCH_V2 §13.2）。
     */
    virtual bool isRefreshingFastData() const = 0;
    /*! 当前 endpoint 的展示名（unix:///...）。 */
    virtual QString endpointDisplayName() const = 0;

    /*! 最近一次成功读取的域数据；模型在对应的 *Updated() 信号后读取。 */
    virtual EngineInfo engineInfo() const = 0;
    virtual QList<Container> containers() const = 0;
    virtual QList<Image> images() const = 0;
    virtual StorageUsage storageUsage() const = 0;
    virtual ContainerDetail containerDetail() const = 0;
    virtual ImageDetail imageDetail() const = 0;
    virtual ContainerStats containerStats() const = 0;
    /*! 某个容器的 stats 是否仍在采样中。 */
    virtual bool isSamplingStats(const QString &id) const = 0;

Q_SIGNALS:
    void engineUpdated();
    void containersUpdated();
    void imagesUpdated();
    void storageUpdated();
    void containerDetailUpdated();
    void imageDetailUpdated();
    void containerStatsUpdated();
    void loadingChanged();
    /*! 某个数据集的失败；任何后端失败都必须是可观察的（ARCH_V1 §23.1）。 */
    void sectionFailed(Kontainer::DockerBackendInterface::Section section, const Kontainer::DockerError &error);
};

} // namespace Kontainer
