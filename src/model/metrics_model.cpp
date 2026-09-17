/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/metrics_model.h"

#include "refresh_policy.h"

#include <QTimer>

namespace Kontainer
{

namespace
{
double ratePerSecond(quint64 current, quint64 previous, qint64 elapsedMs)
{
    if (elapsedMs <= 0) {
        return -1.0; // 无法计算
    }
    if (current < previous) {
        return -1.0; // 计数器回绕 / 容器重启：不给错误数字（§44）
    }
    return double(current - previous) * 1000.0 / double(elapsedMs);
}
} // namespace

MetricsModel::MetricsModel(QObject *parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
{
    // 采样节奏集中来自 RefreshPolicy（§13.1：禁止散落 5000）
    m_timer->setInterval(std::chrono::duration_cast<std::chrono::milliseconds>(RefreshPolicy::kStatsSampleInterval));
    connect(m_timer, &QTimer::timeout, this, &MetricsModel::requestSample);
}

void MetricsModel::requestSample()
{
    if (!m_sampling || m_containerId.isEmpty() || !m_backend) {
        return;
    }
    m_backend->requestContainerStats(m_containerId);
}

void MetricsModel::setBackend(DockerBackendInterface *backend)
{
    m_backend = backend;
}

void MetricsModel::start(const QString &containerId)
{
    if (containerId.isEmpty()) {
        return;
    }
    if (m_sampling && m_containerId == containerId) {
        return; // 已经在采样同一个容器
    }
    stop();

    m_containerId = containerId;
    m_sampling = true;
    m_consecutiveFailures = 0;
    m_points.clear();
    m_hasPrevious = false;
    Q_EMIT stateChanged();
    Q_EMIT updated();

    if (m_backend) {
        m_backend->requestContainerStats(containerId);
        m_timer->start();
    }
}

void MetricsModel::stop()
{
    if (!m_sampling && m_points.isEmpty()) {
        return;
    }
    if (m_backend && !m_containerId.isEmpty()) {
        // 只结束本地采样兴趣，不属于 Docker mutation（§42）
        m_backend->stopContainerStats(m_containerId);
    }
    m_timer->stop();
    m_sampling = false;
    m_containerId.clear();
    // 历史只在内存里，离开页面即释放（§21/§27）
    m_points.clear();
    m_hasPrevious = false;
    m_previous = ContainerStats();
    Q_EMIT stateChanged();
    Q_EMIT updated();
}

bool MetricsModel::memoryLimitEffectiveFor(quint64 limit) const
{
    if (limit == 0) {
        return false;
    }
    const EngineInfo engine = m_backend ? m_backend->engineInfo() : EngineInfo();
    if (engine.memoryTotalBytes > 0 && limit >= quint64(engine.memoryTotalBytes)) {
        return false;
    }
    return true;
}

bool MetricsModel::memoryLimitEffective() const
{
    if (m_points.isEmpty()) {
        return false;
    }
    // Docker 在没有设置内存限制时会把 limit 报成宿主内存总量：
    // 这种“限制”对用户没有意义，不应据此显示百分比（§19）。
    return memoryLimitEffectiveFor(m_points.last().memoryLimitBytes);
}

MetricsPoint MetricsModel::pointFrom(const ContainerStats &stats) const
{
    MetricsPoint point;
    point.timestamp = stats.timestamp;
    point.cpuPercent = stats.cpuPercent();
    point.memoryUsedBytes = stats.memoryUsedBytes();
    point.memoryLimitBytes = stats.memoryLimitBytes;
    point.memoryPercent = stats.memoryPercent();

    if (m_hasPrevious && m_previous.containerId == stats.containerId) {
        const qint64 elapsedMs = m_previous.timestamp.msecsTo(stats.timestamp);
        point.networkRxPerSecond = ratePerSecond(stats.networkRxBytes, m_previous.networkRxBytes, elapsedMs);
        point.networkTxPerSecond = ratePerSecond(stats.networkTxBytes, m_previous.networkTxBytes, elapsedMs);
        point.blockReadPerSecond = ratePerSecond(stats.blockReadBytes, m_previous.blockReadBytes, elapsedMs);
        point.blockWritePerSecond = ratePerSecond(stats.blockWriteBytes, m_previous.blockWriteBytes, elapsedMs);
    }
    return point;
}

void MetricsModel::addSample(const ContainerStats &stats)
{
    if (!m_sampling || stats.containerId != m_containerId) {
        return; // 页面已经离开或不是当前容器：丢弃
    }

    m_consecutiveFailures = 0;
    MetricsPoint point = pointFrom(stats);
    m_previous = stats;
    m_hasPrevious = true;

    if (!memoryLimitEffectiveFor(point.memoryLimitBytes)) {
        point.memoryPercent = -1.0;
    }
    m_points.append(point);
    while (m_points.size() > RefreshPolicy::kMetricsHistorySamples) {
        m_points.removeFirst();
    }
    Q_EMIT updated();
}

void MetricsModel::noteFailure()
{
    if (!m_sampling) {
        return;
    }
    ++m_consecutiveFailures;
    if (m_consecutiveFailures >= RefreshPolicy::kMaxConsecutiveStatsFailures) {
        // 容器可能已经停止：停止轮询，避免无意义请求（§27）
        stop();
    }
}

double MetricsModel::cpuPercent() const
{
    return m_points.isEmpty() ? 0.0 : m_points.last().cpuPercent;
}

qint64 MetricsModel::memoryUsedBytes() const
{
    return m_points.isEmpty() ? 0 : qint64(m_points.last().memoryUsedBytes);
}

qint64 MetricsModel::memoryLimitBytes() const
{
    return m_points.isEmpty() ? 0 : qint64(m_points.last().memoryLimitBytes);
}

double MetricsModel::memoryPercent() const
{
    return m_points.isEmpty() ? -1.0 : m_points.last().memoryPercent;
}

double MetricsModel::networkRxPerSecond() const
{
    return m_points.isEmpty() ? -1.0 : m_points.last().networkRxPerSecond;
}

double MetricsModel::networkTxPerSecond() const
{
    return m_points.isEmpty() ? -1.0 : m_points.last().networkTxPerSecond;
}

double MetricsModel::blockReadPerSecond() const
{
    return m_points.isEmpty() ? -1.0 : m_points.last().blockReadPerSecond;
}

double MetricsModel::blockWritePerSecond() const
{
    return m_points.isEmpty() ? -1.0 : m_points.last().blockWritePerSecond;
}

int MetricsModel::historyCapacity() const
{
    return RefreshPolicy::kMetricsHistorySamples;
}

int MetricsModel::sampleCount() const
{
    return int(m_points.size());
}

QVariantList MetricsModel::series(bool cpu, bool network) const
{
    QVariantList values;
    values.reserve(m_points.size());
    for (const MetricsPoint &point : m_points) {
        if (cpu) {
            values.append(point.cpuPercent);
        } else if (network) {
            // 网络趋势用收发之和，速率未知时用 0 占位（QML 自己决定是否显示）
            const double rx = point.networkRxPerSecond > 0 ? point.networkRxPerSecond : 0.0;
            const double tx = point.networkTxPerSecond > 0 ? point.networkTxPerSecond : 0.0;
            values.append(rx + tx);
        } else {
            values.append(point.memoryUsedBytes);
        }
    }
    return values;
}

QVariantList MetricsModel::cpuHistory() const
{
    return series(true, false);
}

QVariantList MetricsModel::networkHistory() const
{
    return series(false, true);
}

QVariantList MetricsModel::memoryHistory() const
{
    return series(false, false);
}

} // namespace Kontainer
