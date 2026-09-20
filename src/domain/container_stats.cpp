/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "domain/container_stats.h"

#include <algorithm>

namespace Kontainer
{

quint64 ContainerStats::memoryUsedBytes() const
{
    // cgroup v1/v2 both count page cache in usage; subtract it for a truthful figure, otherwise
    // memory usage looks wildly inflated (the Docker CLI computes it the same way).
    return memoryUsageBytes > memoryCacheBytes ? memoryUsageBytes - memoryCacheBytes : 0;
}

double ContainerStats::cpuPercent() const
{
    if (onlineCpus <= 0 || systemCpuUsage == 0) {
        return 0.0;
    }
    // Counter rollback or garbled sampling (§44): return 0, not a negative or absurd number
    if (cpuTotalUsage < cpuPreTotalUsage || systemCpuUsage < systemPreCpuUsage) {
        return 0.0;
    }

    const quint64 cpuDelta = cpuTotalUsage - cpuPreTotalUsage;
    const quint64 systemDelta = systemCpuUsage - systemPreCpuUsage;
    if (systemDelta == 0) {
        return 0.0;
    }

    return (double(cpuDelta) / double(systemDelta)) * double(onlineCpus) * 100.0;
}

double ContainerStats::memoryPercent() const
{
    if (memoryLimitBytes == 0) {
        return -1.0; // no effective limit: show no fake percentage (§19)
    }
    return (double(memoryUsedBytes()) / double(memoryLimitBytes)) * 100.0;
}

} // namespace Kontainer
