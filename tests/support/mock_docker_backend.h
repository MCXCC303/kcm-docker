/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/docker_backend_interface.h"

#include <QHash>
#include <QSet>

namespace Kontainer
{

/*!
 * 测试用的 backend（ARCH_V1 §30）。
 *
 * 让 model / controller 的测试不依赖真实 Docker daemon：
 *  - 测试设置“将要读到”的数据
 *  - refresh*() 只记录请求并进入 loading，由测试调用 completeRefresh() 决定何时完成
 *  - 可以为某个 dataset 注入失败，用来验证错误隔离
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
    /*! 最近一次刷新是否要求统计占用（界面在"只要名字"时可以关掉）。 */
    bool lastVolumesRefreshUsedUsage() const
    {
        return m_lastVolumesIncludeUsage;
    }
    /*! 最近一次创建网络请求（断言校验与请求内容）。 */
    NetworkCreateRequest lastNetworkCreate() const
    {
        return m_lastNetworkCreate;
    }
    /*! 最近一次删除的网络 id。 */
    QString lastRemovedNetwork() const
    {
        return m_lastRemovedNetwork;
    }
    void setStorageUsage(const StorageUsage &usage);
    void setContainerDetail(const ContainerDetail &detail);
    /*! 按 id 存一份详情（渲染工具会同时准备"运行中"与"已暂停"两个页面）。 */
    void setContainerDetailForId(const QString &id, const ContainerDetail &detail);
    void setImageDetail(const ImageDetail &detail);
    void setContainerStats(const ContainerStats &stats);
    void setEndpointName(const QString &name);
    /*! 权限门（DockerCapabilities）会读这个 endpoint；测试用它构造可写 / 不可写场景。 */
    void setEndpoint(const DockerEndpoint &endpoint);

    /*! 让指定 dataset 的下一次刷新失败。 */
    void setNextFailure(Section section, const DockerError &error);
    void clearFailures();

    /* --- 写操作（ARCH_V4 §2.2.4） --- */
    struct MutationCall {
        Mutation mutation = Mutation::StartContainer;
        QString targetKey;
        /*! 删除镜像时的 force 标记。 */
        bool force = false;
    };
    /*! 已发出但还没结束的写操作。 */
    QList<MutationCall> mutationCalls() const
    {
        return m_mutationCalls;
    }
    int mutationCount(Mutation mutation) const;
    QString lastMutationTarget(Mutation mutation) const;
    /*! 被请求取消的引用（按引用取消，ARCH_V4 §2.4）。 */
    QStringList cancelledPulls() const
    {
        return m_cancelledPulls;
    }
    int cancelAllCount() const
    {
        return m_cancelAllCount;
    }
    /*! 结束全部在途写操作（默认成功）；可指定结果与错误。 */
    void completeMutations(MutationOutcome outcome = MutationOutcome::Succeeded, const DockerError &error = DockerError());
    /*! 只结束某一个目标的操作（用于「一路拉取结束、另一路继续」这类场景）。 */
    void completeMutation(const QString &targetKey, MutationOutcome outcome, const DockerError &error = DockerError());
    /*! 模拟引擎推送一条拉取进度。 */
    void emitPullProgress(const ImagePullProgress &progress);

    /*! 结束当前这一轮刷新：发出 *Updated / sectionFailed / loadingChanged。 */
    void completeRefresh();

    int refreshCount(Section section) const;
    /*! 仍在采样 stats 的容器 id（验证详情页生命周期，§27）。 */
    QSet<QString> samplingIds() const
    {
        return m_statsWanted;
    }

    // DockerBackendInterface
    void refreshEngine() override;
    void refreshContainers() override;
    void refreshImages() override;
    void refreshNetworks() override;

    /*! 网络列表被主动刷新过几次（创建容器入口/向导应当触发一次）。 */
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

    /*! 最近一次构建请求（断言查询参数与凭据头真的传下去了）。 */
    ImageBuildRequest lastBuildRequest() const
    {
        return m_lastBuildRequest;
    }
    /*! 让一路构建推进/结束（真实后端从流里读，这里由用例直接喂）。 */
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

    /*! 最近一次创建容器的请求（断言表单字段真的传下去了）。 */
    ContainerCreateRequest lastContainerCreate() const
    {
        return m_lastContainerCreate;
    }
    /*! 让"创建成功"的 id 回来（真实后端在响应里拿到它，再经 containerCreated 发出）。 */
    void completeContainerCreate(const QString &id, const QString &warning = {});
    void removeVolume(const QString &name) override;
    void pruneVolumes() override;

    /*! 最近一次创建的卷参数与删除的卷名（断言参数传递与校验）。 */
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
    /*! 让 prune 的"成功明细"回来（真实后端在响应里拿到删除列表与回收空间）。 */
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

    /*! 最近一次连接/断开请求（断言参数传递）。 */
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

    /* --- 日志流的注入与观察（ARCH_V5_V8 §3.1） --- */

    /*! 把一段日志行当成"引擎推来的"发出去。 */
    void emitLogLines(const QString &id, const QList<Kontainer::LogLine> &lines);
    /*! 结束日志流（默认自然结束）。 */
    void finishLogs(const QString &id, LogStreamEnd end = LogStreamEnd::Ended, const Kontainer::DockerError &error = {});
    /*! 最近一次 startContainerLogs 的参数。 */
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

    /* --- 认证校验的注入与观察（ARCH_V5_V8 §2.6） --- */

    /*! 下线一次校验的结果（默认成功）。 */
    void setAuthCheckResult(AuthCheckResult result, const QString &detail = {});
    /*!
     * 是否延迟回应校验（默认 false = 立即回应）。
     *
     * 只有需要观察"校验进行中"的用例才打开它，否则每个用例都要记得补一次
     * `completeAuthCheck()`，很容易写出假通过的测试。
     */
    void setAuthCheckDeferred(bool deferred)
    {
        m_authCheckDeferred = deferred;
    }
    /*! 把已排队的校验结果发出去（模拟引擎在那之后才回应）。 */
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
    /*! 最近一次拉取带上的凭据（断言"钱包里的凭据真的传到了拉取路径"）。 */
    RegistryCredential lastPullCredential() const
    {
        return m_lastPullCredential;
    }
    bool isLoading() const override;
    /*! 看门狗兜底：把挂起的请求全部按失败送出（与真实后端的语义一致）。 */
    void abandonInFlightRequests(const Kontainer::DockerError &error) override;

    /*! 让请求"永远不完成"：用于验证看门狗（默认立即完成）。 */
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
