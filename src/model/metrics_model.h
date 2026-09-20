/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/docker_backend_interface.h"
#include "domain/container_stats.h"

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QVariantList>

class QTimer;

namespace Kontainer
{

/*! One sample as the presentation layer sees it (rates/percentages precomputed; no QML math, §18). */
struct MetricsPoint {
    QDateTime timestamp;
    double cpuPercent = 0.0;
    quint64 memoryUsedBytes = 0;
    quint64 memoryLimitBytes = 0;
    double memoryPercent = -1.0; /*!< < 0 = no effective limit */
    double networkRxPerSecond = -1.0; /*!< < 0 = not computable yet (first sample / counter wrap) */
    double networkTxPerSecond = -1.0;
    double blockReadPerSecond = -1.0;
    double blockWritePerSecond = -1.0;
};

/*!
 * Container resource metrics model (ARCH_V2 §17–§22/§27/§44).
 *
 * - in-memory short ring buffer only (default 60 × 5s ≈ 5 minutes), no disk, no history database (§21)
 * - rates come from differencing adjacent samples; the first sample and counter wraps are marked
 *   "unknown" (-1) instead of inventing numbers
 * - stop() when the page is left: stop sampling and drop the history (§27)
 */
class MetricsModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool sampling READ sampling NOTIFY stateChanged)
    Q_PROPERTY(bool hasData READ hasData NOTIFY updated)
    Q_PROPERTY(double cpuPercent READ cpuPercent NOTIFY updated)
    Q_PROPERTY(qint64 memoryUsedBytes READ memoryUsedBytes NOTIFY updated)
    Q_PROPERTY(qint64 memoryLimitBytes READ memoryLimitBytes NOTIFY updated)
    Q_PROPERTY(double memoryPercent READ memoryPercent NOTIFY updated)
    /*! Whether a "real" memory limit exists (host-memory-sized limits count as unlimited, §19). */
    Q_PROPERTY(bool memoryLimitEffective READ memoryLimitEffective NOTIFY updated)
    Q_PROPERTY(double networkRxPerSecond READ networkRxPerSecond NOTIFY updated)
    Q_PROPERTY(double networkTxPerSecond READ networkTxPerSecond NOTIFY updated)
    Q_PROPERTY(double blockReadPerSecond READ blockReadPerSecond NOTIFY updated)
    Q_PROPERTY(double blockWritePerSecond READ blockWritePerSecond NOTIFY updated)
    Q_PROPERTY(int sampleCount READ sampleCount NOTIFY updated)
    /*!
     * Ring buffer capacity (= RefreshPolicy::kMetricsHistorySamples).
     *
     * The trend chart renders a fixed number of slots (missing ones stay empty), so the Repeater's
     * model never changes and sampling neither destroys nor creates bars — the root fix for the
     * crash path in ARCH_V3 appendix A.1g.
     */
    Q_PROPERTY(int historyCapacity READ historyCapacity CONSTANT)
    Q_PROPERTY(QVariantList cpuHistory READ cpuHistory NOTIFY updated)
    Q_PROPERTY(QVariantList networkHistory READ networkHistory NOTIFY updated)
    Q_PROPERTY(QVariantList memoryHistory READ memoryHistory NOTIFY updated)

public:
    explicit MetricsModel(QObject *parent = nullptr);

    void setBackend(DockerBackendInterface *backend);

    /*! Start sampling the given container (entering the detail page). */
    void start(const QString &containerId);
    /*! Stop sampling and drop the history (leaving the detail page). */
    void stop();

    bool sampling() const
    {
        return m_sampling;
    }
    bool hasData() const
    {
        return !m_points.isEmpty();
    }
    QString containerId() const
    {
        return m_containerId;
    }

    double cpuPercent() const;
    qint64 memoryUsedBytes() const;
    qint64 memoryLimitBytes() const;
    double memoryPercent() const;
    bool memoryLimitEffective() const;
    double networkRxPerSecond() const;
    double networkTxPerSecond() const;
    double blockReadPerSecond() const;
    double blockWritePerSecond() const;
    int historyCapacity() const;
    int sampleCount() const;
    QVariantList cpuHistory() const;
    QVariantList networkHistory() const;
    QVariantList memoryHistory() const;

    /*! Called by the controller when a new raw sample arrives. */
    void addSample(const ContainerStats &stats);
    /*! Sample timer fired (the interval comes from RefreshPolicy; no scattered magic numbers). */
    void requestSample();
    /*! Sampling failed: used to stop polling a container that stopped or vanished. */
    void noteFailure();

Q_SIGNALS:
    void stateChanged();
    void updated();

private:
    MetricsPoint pointFrom(const ContainerStats &stats) const;
    bool memoryLimitEffectiveFor(quint64 limit) const;
    QVariantList series(bool cpu, bool network) const;

    DockerBackendInterface *m_backend = nullptr;
    QTimer *m_timer = nullptr;
    QString m_containerId;
    bool m_sampling = false;
    QList<MetricsPoint> m_points;
    ContainerStats m_previous;
    bool m_hasPrevious = false;
    int m_consecutiveFailures = 0;
};

} // namespace Kontainer
