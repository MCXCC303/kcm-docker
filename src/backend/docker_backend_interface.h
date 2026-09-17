/*
    SPDX-FileCopyrightText: 2026 kontainer developers
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
/*! 数据卷：名字即标识（Docker 的卷没有独立 id）。 */
inline QString volume(const QString &name)
{
    return QStringLiteral("volume:") + name;
}
/*! 数据卷清理（prune）不是针对某一个对象，但同样要走"目标忙碌"跟踪。 */
inline QString volumePrune()
{
    return QStringLiteral("volume-prune");
}
/*! 网络：用**名字或 id** 都行（创建时只有名字，删除时用 id）。 */
inline QString network(const QString &nameOrId)
{
    return QStringLiteral("network:") + nameOrId;
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
        /*! 网络列表（六期，§3.2）。 */
        Networks,
        /*! 数据卷列表（六期，§3.5）。 */
        Volumes,
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
        /*! 六期：创建 / 删除网络（§3.3）与连接 / 断开容器（§3.4）。 */
        CreateNetwork,
        RemoveNetwork,
        ConnectNetwork,
        DisconnectNetwork,
        /*! 七期：创建容器（§4.6）。 */
        CreateContainer,
        /*! 八期：从 Dockerfile 构建镜像（§5.3）。 */
        BuildImage,
        /*! 八期：清理构建缓存（§5.5）。 */
        PruneBuildCache,
        /*! 六期：数据卷的创建 / 删除 / 清理（§3.5）。 */
        CreateVolume,
        RemoveVolume,
        PruneVolumes,
    };
    Q_ENUM(Mutation)

    /*!
     * 仓库凭据校验的结果（ARCH_V5_V8 §2.6）。
     *
     * 分四类而不是"成功/失败"：界面要给出的下一步完全不同——
     * 凭据错让人重填，仓库不可达让人查网络/代理，引擎不可用则不是用户能修的。
     */
    enum class AuthCheckResult {
        Succeeded,
        /*! 401/403：用户名、密码或令牌不对。 */
        InvalidCredentials,
        /*! 引擎联系不上仓库（DNS / 连接被拒 / TLS / 代理 / 超时）。 */
        RegistryUnreachable,
        /*! 其他失败（引擎自身 5xx、响应无法解析等）。 */
        Failed,
    };
    Q_ENUM(AuthCheckResult)

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
    /*! 网络列表（六期）。低频：变化只有"创建/删除/连接/断开"这几种，按需刷新即可。 */
    virtual void refreshNetworks() = 0;
    /*!
     * 数据卷列表（六期）。同样是低频数据，按需刷新即可。
     *
     * `includeUsage` 关掉时引擎不扫占用（快，但没有大小/引用数）——界面在只需要名字时用它。
     */
    virtual void refreshVolumes(bool includeUsage = true) = 0;
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
    /*!
     * 拉取镜像。
     *
     * `credential` 是给**该镜像所在仓库**的凭据（可为空 = 匿名拉取）。
     * 它只用于这一次请求的 `X-Registry-Auth` 头：不落盘、不进日志。
     */
    virtual void pullImage(const QString &reference, const Kontainer::RegistryCredential &credential = {}) = 0;
    /*! 取消某个在途拉取；没有该拉取时是空操作。 */
    virtual void cancelImagePull(const QString &reference) = 0;
    /*! 取消全部在途拉取（关闭 KCM / 退出时的兜底）。 */
    virtual void cancelAllImagePulls() = 0;
    /*! 取消一路构建（八期 §5.3）：中断上传/响应，临时上下文由后端清理。 */
    virtual void cancelImageBuild(const QString &buildId) = 0;
    /*! `force=true` 用于多标签镜像的强制删除（引擎在 409 时要求）。 */
    virtual void removeImage(const QString &id, bool force) = 0;

    /*!
     * 创建一个网络（ARCH_V5_V8 §3.3）。
     *
     * 目前只做 bridge：请求结构里虽然带 `driver`，但界面只允许 bridge，
     * 其它驱动"只识别不创建"（有意偏离，见 §3.3）。
     */
    virtual void createNetwork(const Kontainer::NetworkCreateRequest &request) = 0;
    /*!
     * 清理构建缓存（`POST /build/prune`，§5.5）。
     *
     * 可回收空间由界面用 `GET /system/df` 里的 BuildCache 预估（见 `StorageUsage`），
     * 真正的回收字节数在 `buildCachePruned()` 里回来。
     */
    virtual void pruneBuildCache() = 0;

    /*!
     * 构建镜像（`POST /build`，§5.3）。
     *
     * 上下文由调用方用 `packBuildContext()` 打好（`request.contextArchive`），
     * 后端负责上传、解析逐行 JSON 进度、结束后删除临时 tar。
     */
    virtual void buildImage(const Kontainer::ImageBuildRequest &request) = 0;

    /*!
     * 创建容器（`POST /containers/create?name=…`，§4.6）。
     *
     * 表单到请求体的映射全在 `ContainerCreateRequest::toJson()` 里（单一实现 + 快照测试）。
     * 新容器的 id 经 `containerCreated()` 回来——`mutationFinished` 只带错误，放不下它。
     */
    virtual void createContainer(const Kontainer::ContainerCreateRequest &request) = 0;

    /*!
     * 创建数据卷（`POST /volumes/create`，§3.5）。
     *
     * 名称校验与重名检查在控制器侧做；这里只负责发请求（`Driver` 默认 local）。
     */
    virtual void createVolume(const QString &name, const QString &driver = {}, const QList<QPair<QString, QString>> &labels = {}) = 0;
    /*!
     * 删除数据卷（`DELETE /volumes/{name}`）。
     *
     * **不传 force**：被容器使用时引擎会拒绝，界面把原因说清楚，而不是替用户强删。
     */
    virtual void removeVolume(const QString &name) = 0;
    /*!
     * 清理未使用的数据卷（`POST /volumes/prune`，§3.5）。
     *
     * 结果（删了哪些、回收了多少空间）经 `volumesPruned()` 回来——`mutationFinished`
     * 只带错误，放不下这份"成功后的明细"。
     */
    virtual void pruneVolumes() = 0;

    /*! 删除网络（`DELETE /networks/{id}`）。内置网络由 daemon 拒绝（403）。 */
    virtual void removeNetwork(const QString &id) = 0;

    /*!
     * 把容器连接到网络（`POST /networks/{id}/connect`，§3.4）。
     *
     * `aliases` 是这个容器在该网络里的别名（Docker 的 `EndpointConfig.Aliases`）：
     * 同一网络里的其它容器可以用别名互相访问，比 IP 稳定。
     */
    virtual void connectNetwork(const QString &networkId, const QString &containerId, const QStringList &aliases = {}) = 0;
    /*!
     * 把容器从网络断开（`POST /networks/{id}/disconnect`，§3.4）。
     *
     * `force` 默认关闭：daemon 只在容器正在运行时才需要它，而"悄悄强制断开"
     * 不是我们想要的默认行为。
     */
    virtual void disconnectNetwork(const QString &networkId, const QString &containerId, bool force = false) = 0;

    /*!
     * 日志流为什么结束（ARCH_V5_V8 §3.1.4）。
     *
     * 分三类而不是"成功/失败"：容器停下来导致的结束是**正常**的（可以重连），
     * 被用户取消也不是错误，只有第三类才需要给出错误文案。
     */
    enum class LogStreamEnd {
        /*! 流自然结束（容器停止、历史读完）。 */
        Ended,
        /*! 读取失败（容器不存在、日志驱动不支持读取、连接中断…）。 */
        Failed,
        /*! 用户取消（离开日志分区 / 点了停止）。 */
        Cancelled,
    };
    Q_ENUM(LogStreamEnd)

    /*!
     * 开始读取日志。
     *
     * `tty` 必须来自容器详情（`Config.Tty`）：TTY 容器输出原始字节，
     * 非 TTY 是 8 字节帧的 stdcopy 流（§3.1.1）。`follow` 为真时持续跟随，
     * 且**不设静默超时**（follow 流可以合法地长时间没有数据）。
     */
    virtual void startContainerLogs(const QString &id, bool tty, bool follow, int tailLines) = 0;
    /*! 停止读取（幂等；没有在跑的流也不报错）。 */
    virtual void stopContainerLogs(const QString &id) = 0;

    /*!
     * 校验一条仓库凭据（`POST /auth`）。
     *
     * 凭据**只走请求头**、只用于这一次请求：不落盘、不进日志、不进错误文案。
     * `serverAddress` 是"要校验哪个仓库"，它决定头里的 `serveraddress` 字段
     * （凭据结构里的同名字段只在参数为空时兜底）。
     * 结果经 `registryAuthChecked()` 回来（异步）。
     */
    virtual void checkRegistryAuth(const QString &serverAddress, const Kontainer::RegistryCredential &credential) = 0;

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
    virtual QList<Network> networks() const = 0;
    virtual QList<Volume> volumes() const = 0;
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
    void networksUpdated();
    void volumesUpdated();
    /*! 构建缓存清理完成：回收的字节数。 */
    void buildCachePruned(qint64 reclaimedBytes);
    /*! 构建进度：`update` 是这一行的增量（step、状态原文、进度）。 */
    void imageBuildProgress(const QString &buildId, const Kontainer::ImageBuildUpdate &update);
    /*! 构建结束：成功时带镜像 id，失败时 `error` 里是**含失败步骤**的原因。 */
    void imageBuildFinished(const QString &buildId,
                            Kontainer::DockerBackendInterface::MutationOutcome outcome,
                            const Kontainer::DockerError &error,
                            const QString &imageId);
    /*! 容器创建成功：新容器 id 与引擎的提醒（`Warnings` 可能非空，例如名称被截断）。 */
    void containerCreated(const QString &id, const QString &warning);
    /*! 数据卷清理完成：删掉的卷名与回收的字节数（可能为空 = 没有可清理的）。 */
    void volumesPruned(const QStringList &names, qint64 reclaimedBytes);
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

    /*! 已经解复用好的日志行（按批发出，避免每个字节都惊动界面）。 */
    void containerLogLines(const QString &id, const QList<Kontainer::LogLine> &lines);
    /*! 日志流结束（含原因；`Failed` 时 `error` 是引擎/传输层给的分类）。 */
    void containerLogsFinished(const QString &id,
                               Kontainer::DockerBackendInterface::LogStreamEnd end,
                               const Kontainer::DockerError &error);

    /*!
     * 仓库凭据校验的结果。
     *
     * `detail` 是引擎原文，只用于日志与"技术细节"（可能含仓库返回的文本），
     * 不当作用户文案——界面按 `result` 取自己的文案。
     */
    void registryAuthChecked(const QString &serverAddress,
                             Kontainer::DockerBackendInterface::AuthCheckResult result,
                             const QString &detail);
};

} // namespace Kontainer
