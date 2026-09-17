/*
    SPDX-FileCopyrightText: 2026 kontainer developers
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
 * 真实的 Docker backend（ARCH_V1 §6.3/§11/§15/§17，ARCH_V2 §29/§42，ARCH_V4 §2.2）。
 *
 * 职责：HTTP 请求、Unix socket、HTTP 状态码、JSON 解码、API 版本处理、
 * 错误映射、请求去重。不含任何 UI 文本，也不知道用户当前在哪个页面（§43）。
 *
 * 读路径：GET /_ping、/version、/info、/containers/json、/images/json、/system/df、
 * /containers/{id}/json、/images/{id}/json、/containers/{id}/stats?stream=false。
 *
 * 四期起新增写操作（ARCH_V4 §2.3/§2.4）：容器 start / stop / restart / remove，
 * 镜像 pull（流式，带进度与取消）/ remove。全部路径来自 `docker_api_paths.h`，
 * 且本类是唯一允许调用 DockerClient 写方法的文件（tst_source_conventions 断言）。
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
    void createContainer(const Kontainer::ContainerCreateRequest &request) override;
    void createVolume(const QString &name, const QString &driver = {}, const QList<QPair<QString, QString>> &labels = {}) override;
    void removeVolume(const QString &name) override;
    void pruneVolumes() override;

    void refreshStorageUsage() override;
    void inspectContainer(const QString &id) override;
    void inspectImage(const QString &id) override;
    void requestContainerStats(const QString &id) override;
    void stopContainerStats(const QString &id) override;

    /* --- 写操作（ARCH_V4 §2.2.4） --- */
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

    /*! `/auth` 的超时（引擎要联系仓库，与写操作同量级）。 */
    int authCheckTimeoutMs() const;


    /* --- 容器日志（流式，ARCH_V5_V8 §3.1） --- */
    struct LogStreamState {
        DockerReply *reply = nullptr;
        LogFrameReader reader;
        /*! 用户是否已经要求停止（用于把结束原因归到"取消"）。 */
        bool cancelled = false;
    };
    /*! 每个容器最多一路日志流（换容器或重连会先停掉旧的）。 */
    QHash<QString, LogStreamState> m_logStreams;
    /*! 历史（follow=0）读取的超时；follow 流不设静默超时。 */
    int logHistoryTimeoutMs() const;
    /*! 还在等版本握手时就被要求停止的日志请求（避免开了流没人收）。 */
    QSet<QString> m_cancelledLogRequests;

    /* --- 镜像拉取（流式，可并发，ARCH_V4 §2.4） --- */

    struct PullLayerState {
        qint64 current = 0;
        qint64 total = 0;
        bool complete = false;
    };

    /*! 一路拉取的全部状态：每路都有自己的流解析器与进度聚合。 */
    struct ImagePullState {
        QString reference;
        QString targetKey;
        DockerReply *reply = nullptr;
        JsonLineReader reader;
        ImagePullProgress progress;
        QHash<QString, PullLayerState> layers;
        bool failed = false;
    };

    /*! 一路构建的状态：自己的流解析器 + 当前进度聚合。 */
    struct ImageBuildState {
        QString id;
        QString contextArchive;
        DockerReply *reply = nullptr;
        JsonLineReader reader;
        ImageBuildUpdate update;
        bool failed = false;
    };

    /*! 需要 API 版本前缀的请求：若尚未协商，则先完成握手再执行。 */
    void withApiVersion(Section section, ReadyCallback callback);

    /*!
     * 写操作的地基：等版本握手完成后执行；握手失败时把错误作为
     * mutation 失败上报（而不是变成一个莫名其妙的 section 错误）。
     */
    void runMutation(Mutation mutation, const QString &targetKey, ReadyCallback run);
    /*! start / stop / restart / remove 共用的实现。 */
    void runContainerMutation(Mutation mutation, const QString &id, const QString &apiPath, const QUrlQuery &query);
    /*! 处理构建流的一行（聚合进度、记录失败原因）。 */
    void handleBuildLine(ImageBuildState &state, const QJsonObject &object);
    /*! 结束一路构建：删临时 tar、清理状态、发信号。 */
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
    bool m_networksInFlight = false;
    bool m_volumesInFlight = false;
    /*! 上一次成功读取的数据卷列表（刷新失败时保留）。 */
    QList<Volume> m_volumes;
    /*! 上一次成功读取的网络列表（刷新失败时保留，界面不会突然空掉）。 */
    QList<Network> m_networks;
    bool m_handshakeInFlight = false;
    bool m_loading = false;

    /*! 在途请求 key 集合（inspect / stats / storage 的去重）。 */
    QSet<QString> m_inFlightRequests;
    /*! 仍然需要 stats 的容器（离开详情页后移除，§27）。 */
    QSet<QString> m_statsWanted;

    QList<QPair<Section, ReadyCallback>> m_readyCallbacks;

    /*! 在途拉取，key = targetKey（`image:<归一化引用>`）。 */
    QHash<QString, ImagePullState> m_pulls;
    QHash<QString, ImageBuildState> m_builds;

    /*! 等待版本握手的写操作（握手完成或失败后统一清算）。 */
    struct PendingMutation {
        Mutation mutation;
        QString targetKey;
        ReadyCallback run;
    };
    QList<PendingMutation> m_pendingMutations;

};

} // namespace Kontainer
