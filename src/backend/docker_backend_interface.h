/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/docker_endpoint.h"
#include "backend/docker_error.h"
#include "domain/container.h"
#include "domain/container_detail.h"
#include "domain/container_stats.h"
#include "domain/engine_info.h"
#include "domain/image.h"
#include "domain/image_detail.h"
#include "domain/image_pull_progress.h"
#include "domain/storage_usage.h"

#include <QList>
#include <QObject>
#include <QString>

namespace Kontainer
{

/*!
 * 写操作目标 key 的唯一构造点（ARCH_V4 §2.2.4）。
 *
 * backend 与 model 必须用同一套前缀：界面用 `isTargetBusy(key)` 判断某个对象
 * 是否有操作在途，两边拼法一旦分叉，忙碌态就会静默失效。
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
} // namespace OperationTarget

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

    /*! 写操作（ARCH_V4 §2.2.4 / §2.3 / §2.4）。 */
    enum class Mutation {
        StartContainer,
        StopContainer,
        RestartContainer,
        RemoveContainer,
        PullImage,
        RemoveImage,
    };
    Q_ENUM(Mutation)

    /*! 取消不是错误，因此结果不能只看 DockerError（ARCH_V4 §2.2.1）。 */
    enum class MutationOutcome {
        Succeeded,
        /*! 引擎返回 304：已经处于目标状态（重复 start / stop）。 */
        Unchanged,
        Failed,
        Cancelled,
    };
    Q_ENUM(MutationOutcome)

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

    /* --- 写操作（ARCH_V4 §2.2.4） ---------------------------------------------
     *
     * 表达「我要改变什么」，不表达 HTTP 细节；调用方是 OperationController，
     * QML 永远不直接调这些方法（ARCH_V4 §1.5 的咽喉点约束）。
     * 每个操作恰好发一次 mutationFinished()；拉取额外发若干次 imagePullProgress()。
     */
    virtual void startContainer(const QString &id) = 0;
    /*! `t` 由实现统一取 RefreshPolicy::kStopTimeoutSeconds，不由 UI 传。 */
    virtual void stopContainer(const QString &id) = 0;
    virtual void restartContainer(const QString &id) = 0;
    /*! 删除容器；不带 `v`（不删卷）、不带 `force`（运行中必须由引擎拒绝）。 */
    virtual void removeContainer(const QString &id) = 0;
    /*!
     * 拉取镜像（ARCH_V4 §2.4）。
     *
     * 支持**并发**：不同引用可以同时在途，互不影响；同一个引用重复拉取会被拒绝
     * （引擎自己也会去重，但在我们这一侧拒绝能给出更清楚的文案）。
     */
    virtual void pullImage(const QString &reference) = 0;
    /*! 取消某个在途拉取；没有该拉取时是空操作。 */
    virtual void cancelImagePull(const QString &reference) = 0;
    /*! 取消全部在途拉取（关闭 KCM / 退出时的兜底）。 */
    virtual void cancelAllImagePulls() = 0;
    /*! `force=true` 用于多标签镜像的强制删除（引擎在 409 时要求）。 */
    virtual void removeImage(const QString &id, bool force) = 0;

    /*! 当前端点：权限门（DockerCapabilities）据此判断可写性。 */
    virtual DockerEndpoint endpoint() const = 0;

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

    /*!
     * 一次写操作的结束（成功 / 失败 / 被取消）。
     * `targetKey` 与 OperationController 的 targetKey 同构：`container:<id>` / `image:<ref>`。
     */
    void mutationFinished(Kontainer::DockerBackendInterface::Mutation mutation,
                          const QString &targetKey,
                          Kontainer::DockerBackendInterface::MutationOutcome outcome,
                          const Kontainer::DockerError &error);
    /*! 拉取进度（引擎每报告一行就聚合一次）。 */
    void imagePullProgress(const Kontainer::ImagePullProgress &progress);
};

} // namespace Kontainer
