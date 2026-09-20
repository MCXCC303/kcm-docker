/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
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
        return -1.0; // cannot compute
    }
    if (current < previous) {
        return -1.0; // counter wrap / container restart: never report a bogus number (§44)
    }
    return double(current - previous) * 1000.0 / double(elapsedMs);
}
} // namespace

MetricsModel::MetricsModel(QObject *parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
{
    // Sampling cadence comes from RefreshPolicy only (§13.1: no scattered 5000 literals)
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
        return; // already sampling this container
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
        // Ends local sampling interest only; not a Docker mutation (§42)
        m_backend->stopContainerStats(m_containerId);
    }
    m_timer->stop();
    m_sampling = false;
    m_containerId.clear();
    // History lives in memory only and is released on leaving the page (§21/§27)
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
    // With no memory limit set, Docker reports the host total as the limit: such a "limit" means
    // nothing to the user and must not drive a percentage (§19).
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
        return; // page left or a different container: drop the sample
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
        // The container may have stopped: stop polling to avoid pointless requests (§27)
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
            // Network trend is rx + tx; unknown rates use 0 as a placeholder (QML decides display)
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
