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

void MockDockerBackend::setStorageUsage(const StorageUsage &usage)
{
    m_storageUsage = usage;
}

void MockDockerBackend::setContainerDetail(const ContainerDetail &detail)
{
    m_containerDetail = detail;
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

void MockDockerBackend::refreshStorageUsage()
{
    beginRefresh(Section::Storage);
}

void MockDockerBackend::inspectContainer(const QString &id)
{
    m_containerDetail.id = id;
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

void MockDockerBackend::pullImage(const QString &reference)
{
    m_mutationCalls.append({Mutation::PullImage, QStringLiteral("image:") + reference, false});
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

void MockDockerBackend::completeRefresh()
{
    const QList<Section> sections = {Section::Engine,
                                     Section::Containers,
                                     Section::Images,
                                     Section::Storage,
                                     Section::ContainerDetail,
                                     Section::ImageDetail,
                                     Section::Stats};
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
