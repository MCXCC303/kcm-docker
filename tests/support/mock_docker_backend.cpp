/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "support/mock_docker_backend.h"

namespace Kontainer
{

MockDockerBackend::MockDockerBackend(QObject *parent)
    : DockerBackendInterface(parent)
{
}

void MockDockerBackend::setEngineInfo(const EngineInfo &info)
{
    m_engine = info;
}

void MockDockerBackend::setContainers(const QList<Container> &containers)
{
    m_containers = containers;
}

void MockDockerBackend::setImages(const QList<Image> &images)
{
    m_images = images;
}

void MockDockerBackend::setNetworks(const QList<Network> &networks)
{
    m_networks = networks;
}

void MockDockerBackend::setVolumes(const QList<Volume> &volumes)
{
    m_volumes = volumes;
}

void MockDockerBackend::setStorageUsage(const StorageUsage &usage)
{
    m_storageUsage = usage;
}

void MockDockerBackend::setContainerDetail(const ContainerDetail &detail)
{
    m_containerDetail = detail;
}

void MockDockerBackend::setContainerDetailForId(const QString &id, const ContainerDetail &detail)
{
    m_containerDetailsById.insert(id, detail);
}

void MockDockerBackend::setImageDetail(const ImageDetail &detail)
{
    m_imageDetail = detail;
}

void MockDockerBackend::setContainerStats(const ContainerStats &stats)
{
    m_containerStats = stats;
}

void MockDockerBackend::setEndpointName(const QString &name)
{
    m_endpointName = name;
    Q_EMIT loadingChanged(); // 让依赖 endpoint 的绑定有机会刷新
}

void MockDockerBackend::setNextFailure(Section section, const DockerError &error)
{
    m_failures.insert(int(section), error);
}

void MockDockerBackend::clearFailures()
{
    m_failures.clear();
}

int MockDockerBackend::refreshCount(Section section) const
{
    return m_refreshCounts.value(int(section));
}

void MockDockerBackend::beginRefresh(Section section)
{
    m_refreshCounts[int(section)] = refreshCount(section) + 1;
    m_pending.insert(int(section), true);
    if (!m_loading) {
        m_loading = true;
        Q_EMIT loadingChanged();
    }
}

void MockDockerBackend::refreshEngine()
{
    beginRefresh(Section::Engine);
}

void MockDockerBackend::refreshContainers()
{
    beginRefresh(Section::Containers);
}

void MockDockerBackend::refreshImages()
{
    beginRefresh(Section::Images);
}

void MockDockerBackend::refreshNetworks()
{
    ++m_networkRefreshCount;
    beginRefresh(Section::Networks);
}

void MockDockerBackend::refreshVolumes(bool includeUsage)
{
    m_lastVolumesIncludeUsage = includeUsage;
    beginRefresh(Section::Volumes);
}

void MockDockerBackend::pauseContainer(const QString &id)
{
    m_mutationCalls.append({Mutation::PauseContainer, OperationTarget::container(id), false});
}

void MockDockerBackend::unpauseContainer(const QString &id)
{
    m_mutationCalls.append({Mutation::UnpauseContainer, OperationTarget::container(id), false});
}

void MockDockerBackend::buildImage(const ImageBuildRequest &request)
{
    m_lastBuildRequest = request;
    m_mutationCalls.append({Mutation::BuildImage, QStringLiteral("build:") + request.id, false});
}

void MockDockerBackend::pruneBuildCache()
{
    m_mutationCalls.append({Mutation::PruneBuildCache, QStringLiteral("buildCache:"), false});
}

void MockDockerBackend::completeBuildCachePrune(qint64 reclaimedBytes)
{
    Q_EMIT buildCachePruned(reclaimedBytes);
}

void MockDockerBackend::cancelImageBuild(const QString &buildId)
{
    m_cancelledBuilds.append(buildId);
}

void MockDockerBackend::emitBuildProgress(const QString &buildId, const ImageBuildUpdate &update)
{
    Q_EMIT imageBuildProgress(buildId, update);
}

void MockDockerBackend::emitBuildFinished(const QString &buildId,
                                          DockerBackendInterface::MutationOutcome outcome,
                                          const DockerError &error,
                                          const QString &imageId)
{
    Q_EMIT imageBuildFinished(buildId, outcome, error, imageId);
}

void MockDockerBackend::createContainer(const ContainerCreateRequest &request)
{
    m_lastContainerCreate = request;
    m_mutationCalls.append({Mutation::CreateContainer, OperationTarget::container(request.name), false});
}

void MockDockerBackend::completeContainerCreate(const QString &id, const QString &warning)
{
    Q_EMIT containerCreated(id, warning);
}

void MockDockerBackend::createVolume(const QString &name, const QString &driver, const QList<QPair<QString, QString>> &labels)
{
    Q_UNUSED(labels);
    m_lastCreatedVolume = {name, driver};
    m_mutationCalls.append({Mutation::CreateVolume, OperationTarget::volume(name), false});
}

void MockDockerBackend::removeVolume(const QString &name)
{
    m_lastRemovedVolume = name;
    m_mutationCalls.append({Mutation::RemoveVolume, OperationTarget::volume(name), false});
}

void MockDockerBackend::pruneVolumes()
{
    ++m_pruneCalls;
    // 目标键与真实后端一致：prune 不是针对某个卷，用专门的键
    m_mutationCalls.append({Mutation::PruneVolumes, OperationTarget::volumePrune(), false});
}

void MockDockerBackend::completePrune(const QStringList &names, qint64 reclaimedBytes)
{
    Q_EMIT volumesPruned(names, reclaimedBytes);
}

void MockDockerBackend::refreshStorageUsage()
{
    beginRefresh(Section::Storage);
}

void MockDockerBackend::inspectContainer(const QString &id)
{
    // 按 id 取准备好的详情（渲染工具会同时准备多份）；没有就沿用最后设置的那份
    if (m_containerDetailsById.contains(id)) {
        m_containerDetail = m_containerDetailsById.value(id);
    } else {
        m_containerDetail.id = id;
    }
    beginRefresh(Section::ContainerDetail);
}

void MockDockerBackend::inspectImage(const QString &id)
{
    m_imageDetail.id = id;
    beginRefresh(Section::ImageDetail);
}

void MockDockerBackend::requestContainerStats(const QString &id)
{
    m_statsWanted.insert(id);
    m_containerStats.containerId = id;
    beginRefresh(Section::Stats);
}

void MockDockerBackend::stopContainerStats(const QString &id)
{
    m_statsWanted.remove(id);
}

bool MockDockerBackend::isSamplingStats(const QString &id) const
{
    return m_statsWanted.contains(id);
}

void MockDockerBackend::setEndpoint(const DockerEndpoint &endpoint)
{
    m_endpoint = endpoint;
    Q_EMIT loadingChanged(); // 让依赖 endpoint 的绑定有机会刷新
}

DockerEndpoint MockDockerBackend::endpoint() const
{
    return m_endpoint;
}

int MockDockerBackend::mutationCount(Mutation mutation) const
{
    int count = 0;
    for (const MutationCall &call : m_mutationCalls) {
        if (call.mutation == mutation) {
            ++count;
        }
    }
    return count;
}

QString MockDockerBackend::lastMutationTarget(Mutation mutation) const
{
    for (int i = m_mutationCalls.size() - 1; i >= 0; --i) {
        if (m_mutationCalls.at(i).mutation == mutation) {
            return m_mutationCalls.at(i).targetKey;
        }
    }
    return {};
}

void MockDockerBackend::completeMutations(MutationOutcome outcome, const DockerError &error)
{
    const QList<MutationCall> calls = std::exchange(m_mutationCalls, {});
    for (const MutationCall &call : calls) {
        Q_EMIT mutationFinished(call.mutation, call.targetKey, outcome, error);
    }
}

void MockDockerBackend::completeMutation(const QString &targetKey, MutationOutcome outcome, const DockerError &error)
{
    for (int i = 0; i < m_mutationCalls.size(); ++i) {
        if (m_mutationCalls.at(i).targetKey == targetKey) {
            const MutationCall call = m_mutationCalls.takeAt(i);
            Q_EMIT mutationFinished(call.mutation, call.targetKey, outcome, error);
            return;
        }
    }
}

void MockDockerBackend::emitPullProgress(const ImagePullProgress &progress)
{
    Q_EMIT imagePullProgress(progress);
}

void MockDockerBackend::startContainer(const QString &id)
{
    m_mutationCalls.append({Mutation::StartContainer, QStringLiteral("container:") + id, false});
}

void MockDockerBackend::stopContainer(const QString &id)
{
    m_mutationCalls.append({Mutation::StopContainer, QStringLiteral("container:") + id, false});
}

void MockDockerBackend::restartContainer(const QString &id)
{
    m_mutationCalls.append({Mutation::RestartContainer, QStringLiteral("container:") + id, false});
}

void MockDockerBackend::removeContainer(const QString &id)
{
    m_mutationCalls.append({Mutation::RemoveContainer, QStringLiteral("container:") + id, false});
}

void MockDockerBackend::pullImage(const QString &reference, const RegistryCredential &credential)
{
    m_lastPullCredential = credential;
    m_mutationCalls.append({Mutation::PullImage, QStringLiteral("image:") + reference, false});
}

void MockDockerBackend::createNetwork(const NetworkCreateRequest &request)
{
    m_lastNetworkCreate = request;
    m_mutationCalls.append({Mutation::CreateNetwork, OperationTarget::network(request.name), false});
}

void MockDockerBackend::removeNetwork(const QString &id)
{
    m_lastRemovedNetwork = id;
    m_mutationCalls.append({Mutation::RemoveNetwork, OperationTarget::network(id), false});
}

void MockDockerBackend::connectNetwork(const QString &networkId, const QString &containerId, const QStringList &aliases)
{
    m_lastNetworkConnect = {networkId, containerId};
    m_lastNetworkConnectAliases = aliases;
    // 目标键与真实后端一致（网络 + 容器）：控制器据此找到要重读的容器
    m_mutationCalls.append({Mutation::ConnectNetwork, OperationTarget::network(networkId) + QLatin1Char('/') + containerId, false});
}

void MockDockerBackend::disconnectNetwork(const QString &networkId, const QString &containerId, bool force)
{
    Q_UNUSED(force);
    m_lastNetworkDisconnect = {networkId, containerId};
    m_mutationCalls.append({Mutation::DisconnectNetwork, OperationTarget::network(networkId) + QLatin1Char('/') + containerId, false});
}

void MockDockerBackend::cancelImagePull(const QString &reference)
{
    m_cancelledPulls.append(reference);
}

void MockDockerBackend::cancelAllImagePulls()
{
    ++m_cancelAllCount;
}

void MockDockerBackend::removeImage(const QString &id, bool force)
{
    m_mutationCalls.append({Mutation::RemoveImage, QStringLiteral("image:") + id, force});
}

void MockDockerBackend::checkRegistryAuth(const QString &serverAddress, const RegistryCredential &credential)
{
    ++m_authCheckCount;
    m_lastAuthServerAddress = serverAddress;
    m_lastAuthCredential = credential;
    // 默认立即回应；需要观察"进行中"的用例用 setAuthCheckDeferred(true) 自己控制时序
    if (!m_authCheckDeferred) {
        completeAuthCheck();
        return;
    }
    m_authCheckPending = true;
}

void MockDockerBackend::setAuthCheckResult(AuthCheckResult result, const QString &detail)
{
    m_authCheckResult = result;
    m_authCheckDetail = detail;
}

void MockDockerBackend::completeAuthCheck()
{
    m_authCheckPending = false;
    Q_EMIT registryAuthChecked(m_lastAuthServerAddress, m_authCheckResult, m_authCheckDetail);
}

void MockDockerBackend::startContainerLogs(const QString &id, bool tty, bool follow, int tailLines)
{
    // 记录参数即可：真实读取由真实后端负责，这里只让控制器/界面能跑起来
    m_lastLogContainerId = id;
    m_lastLogTty = tty;
    m_lastLogFollow = follow;
    m_lastLogTailLines = tailLines;
}

void MockDockerBackend::stopContainerLogs(const QString &id)
{
    ++m_stoppedLogStreams[id];
}

void MockDockerBackend::emitLogLines(const QString &id, const QList<LogLine> &lines)
{
    Q_EMIT containerLogLines(id, lines);
}

void MockDockerBackend::finishLogs(const QString &id, LogStreamEnd end, const DockerError &error)
{
    Q_EMIT containerLogsFinished(id, end, error);
}

void MockDockerBackend::completeRefresh()
{
    const QList<Section> sections = {Section::Engine,
                                     Section::Containers,
                                     Section::Images,
                                     Section::Storage,
                                     Section::ContainerDetail,
                                     Section::ImageDetail,
                                     Section::Stats,
                                     Section::Networks,
                                     Section::Volumes};
    for (Section section : sections) {
        if (!m_pending.value(int(section))) {
            continue;
        }
        m_pending.insert(int(section), false);

        const DockerError failure = m_failures.take(int(section));
        if (failure.isError()) {
            // 与真实 backend 一致：Engine 的"部分失败"（连接可用、/info 失败）
            // 仍然先给出已有的域数据，再上报该数据集的失败（错误隔离，§15）
            if (section == Section::Engine && m_engine.available) {
                Q_EMIT engineUpdated();
            }
            Q_EMIT sectionFailed(section, failure);
            continue;
        }
        switch (section) {
        case Section::Engine:
            Q_EMIT engineUpdated();
            break;
        case Section::Containers:
            Q_EMIT containersUpdated();
            break;
        case Section::Images:
            Q_EMIT imagesUpdated();
            break;
        case Section::Storage:
            Q_EMIT storageUpdated();
            break;
        case Section::ContainerDetail:
            Q_EMIT containerDetailUpdated();
            break;
        case Section::ImageDetail:
            Q_EMIT imageDetailUpdated();
            break;
        case Section::Stats:
            Q_EMIT containerStatsUpdated();
            break;
        case Section::Networks:
            Q_EMIT networksUpdated();
            break;
        case Section::Volumes:
            Q_EMIT volumesUpdated();
            break;
        }
    }

    m_loading = false;
    Q_EMIT loadingChanged();
}

bool MockDockerBackend::isLoading() const
{
    return m_loading;
}

bool MockDockerBackend::isRefreshingFastData() const
{
    return m_pending.value(int(Section::Engine)) || m_pending.value(int(Section::Containers)) || m_pending.value(int(Section::Images));
}

QString MockDockerBackend::endpointDisplayName() const
{
    return m_endpointName;
}

EngineInfo MockDockerBackend::engineInfo() const
{
    return m_engine;
}

QList<Container> MockDockerBackend::containers() const
{
    return m_containers;
}

QList<Image> MockDockerBackend::images() const
{
    return m_images;
}

StorageUsage MockDockerBackend::storageUsage() const
{
    return m_storageUsage;
}

ContainerDetail MockDockerBackend::containerDetail() const
{
    return m_containerDetail;
}

ImageDetail MockDockerBackend::imageDetail() const
{
    return m_imageDetail;
}

ContainerStats MockDockerBackend::containerStats() const
{
    return m_containerStats;
}

} // namespace Kontainer
