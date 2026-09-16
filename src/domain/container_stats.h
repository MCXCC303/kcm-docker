/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QDateTime>
#include <QMetaType>

namespace Kontainer
{

/*!
 * 容器资源统计的**单次原始采样**（ARCH_V2 §17/§17.2/§18）。
 *
 * 内部只保存原始数值（字节、纳秒、累加计数器），不生成 "1.8 GiB" / "12.3%" 这类
 * UI 字符串。CPU 百分比等公式属于业务逻辑，由 metrics layer 计算（§18）。
 */
struct ContainerStats {
    QString containerId;
    QDateTime timestamp;

    /* --- CPU（Docker stats 的 cpu_stats + precpu_stats） --- */
    quint64 cpuTotalUsage = 0;
    quint64 cpuPreTotalUsage = 0;
    quint64 systemCpuUsage = 0;
    quint64 systemPreCpuUsage = 0;
    int onlineCpus = 0;

    /* --- 内存（cgroup v1/v2 的差异在这个结构里被抹平） --- */
    quint64 memoryUsageBytes = 0; /*!< 含 page cache 的原始 usage */
    quint64 memoryCacheBytes = 0; /*!< v1: cache/total_inactive_file；v2: inactive_file */
    quint64 memoryLimitBytes = 0; /*!< 0 表示没有有效限制 */

    /* --- 累计计数器 --- */
    quint64 networkRxBytes = 0;
    quint64 networkTxBytes = 0;
    quint64 blockReadBytes = 0;
    quint64 blockWriteBytes = 0;
    int pids = 0;

    bool isValid() const
    {
        return !containerId.isEmpty();
    }

    /*! 去掉 page cache 之后的内存占用（不小于 0）。 */
    quint64 memoryUsedBytes() const;

    /*!
     * CPU 使用率百分比（0..N×100，N = 在线 CPU 数）。
     *
     * 采用 Docker 官方算法：(cpuDelta / systemDelta) × onlineCpus × 100。
     * 单次 `?stream=false` 采样同时包含 cpu_stats 与 precpu_stats，因此一次采样即可计算。
     */
    double cpuPercent() const;

    /*!
     * 内存使用率百分比；没有有效 limit 时返回 -1（§19：不显示虚假百分比）。
     */
    double memoryPercent() const;
};

} // namespace Kontainer

Q_DECLARE_METATYPE(Kontainer::ContainerStats)
