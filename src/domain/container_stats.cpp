/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "domain/container_stats.h"

#include <algorithm>

namespace Kontainer
{

quint64 ContainerStats::memoryUsedBytes() const
{
    // cgroup v1/v2 都会把 page cache 计入 usage；展示“真实占用”时需要扣除，
    // 否则内存使用率会严重虚高（Docker CLI 也是这么算的）。
    return memoryUsageBytes > memoryCacheBytes ? memoryUsageBytes - memoryCacheBytes : 0;
}

double ContainerStats::cpuPercent() const
{
    if (onlineCpus <= 0 || systemCpuUsage == 0) {
        return 0.0;
    }
    // 计数器回绕 / 采样错乱（§44 counter rollback）：返回 0 而不是负数或天文数字
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
        return -1.0; // 没有有效 limit：不显示虚假百分比（§19）
    }
    return (double(memoryUsedBytes()) / double(memoryLimitBytes)) * 100.0;
}

} // namespace Kontainer
