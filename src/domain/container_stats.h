/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QDateTime>
#include <QMetaType>

namespace Kontainer
{

/*!
 * A **single raw sample** of container resource stats (ARCH_V2 §17/§17.2/§18).
 *
 * Holds raw numbers only (bytes, nanoseconds, cumulative counters), never strings like "1.8 GiB" /
 * "12.3%". Formulas such as CPU percentage are business logic and live in the metrics layer (§18).
 */
struct ContainerStats {
    QString containerId;
    QDateTime timestamp;

    /* --- CPU (Docker stats' cpu_stats + precpu_stats) --- */
    quint64 cpuTotalUsage = 0;
    quint64 cpuPreTotalUsage = 0;
    quint64 systemCpuUsage = 0;
    quint64 systemPreCpuUsage = 0;
    int onlineCpus = 0;

    /* --- Memory (cgroup v1/v2 differences are smoothed out in this struct) --- */
    quint64 memoryUsageBytes = 0; /*!< raw usage, page cache included */
    quint64 memoryCacheBytes = 0; /*!< v1: cache/total_inactive_file; v2: inactive_file */
    quint64 memoryLimitBytes = 0; /*!< 0 means no effective limit */

    /* --- Cumulative counters --- */
    quint64 networkRxBytes = 0;
    quint64 networkTxBytes = 0;
    quint64 blockReadBytes = 0;
    quint64 blockWriteBytes = 0;
    int pids = 0;

    bool isValid() const
    {
        return !containerId.isEmpty();
    }

    /*! Memory in use after subtracting page cache (never below 0). */
    quint64 memoryUsedBytes() const;

    /*!
     * CPU usage percentage (0..N×100, N = online CPUs).
     *
     * Uses Docker's own formula: (cpuDelta / systemDelta) × onlineCpus × 100. A single
     * `?stream=false` sample carries cpu_stats and precpu_stats together, so one sample suffices.
     */
    double cpuPercent() const;

    /*!
     * Memory usage percentage; -1 when there is no effective limit (§19: no fake percentages).
     */
    double memoryPercent() const;
};

} // namespace Kontainer

Q_DECLARE_METATYPE(Kontainer::ContainerStats)
