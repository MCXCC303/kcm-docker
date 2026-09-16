/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "dto/stats_dto.h"

#include "dto/json_helpers.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace Kontainer
{

using namespace JsonHelpers;

namespace
{

/*! 取嵌套对象里的 64 位计数（缺失或类型不符时返回 0）。 */
quint64 nestedCounter(const QJsonObject &object, const QString &outer, const QString &inner)
{
    const QJsonValue value = object.value(outer);
    if (!value.isObject()) {
        return 0;
    }
    const qint64 raw = integerValue(value.toObject(), inner);
    return raw > 0 ? quint64(raw) : 0;
}

/*!
 * cgroup v1 用 cache / total_inactive_file，cgroup v2 用 inactive_file 表示 page cache。
 * 三者取第一个存在的值，抹平版本差异（§17.2：内部保存原始语义）。
 */
quint64 parseMemoryCacheBytes(const QJsonObject &memoryStats)
{
    const QJsonObject stats = memoryStats.value(QStringLiteral("stats")).toObject();
    for (const QString &key : {QStringLiteral("inactive_file"), QStringLiteral("total_inactive_file"), QStringLiteral("cache")}) {
        const QJsonValue value = stats.value(key);
        if (value.isDouble()) {
            const qint64 raw = qint64(value.toDouble());
            if (raw > 0) {
                return quint64(raw);
            }
        }
    }
    return 0;
}

quint64 networkBytes(const QJsonObject &object, const QString &key)
{
    quint64 total = 0;
    const QJsonObject networks = object.value(QStringLiteral("networks")).toObject();
    for (auto it = networks.constBegin(); it != networks.constEnd(); ++it) {
        if (!it.value().isObject()) {
            continue;
        }
        const qint64 value = integerValue(it.value().toObject(), key);
        if (value > 0) {
            total += quint64(value);
        }
    }
    return total;
}

/*!
 * blkio_stats.io_service_bytes_recursive 的 op 可能是 read/write，也可能是 sync/async/total。
 * 优先用精确的 read/write，避免把 sync+async 重复计入；只有在完全没有 read/write 时才回退。
 */
void blockIoBytes(const QJsonObject &object, quint64 &readBytes, quint64 &writeBytes)
{
    const QJsonArray entries = object.value(QStringLiteral("blkio_stats")).toObject().value(QStringLiteral("io_service_bytes_recursive")).toArray();

    quint64 exactRead = 0;
    quint64 exactWrite = 0;
    quint64 syncBytes = 0;
    quint64 asyncBytes = 0;
    for (const QJsonValue &entry : entries) {
        if (!entry.isObject()) {
            continue;
        }
        const QJsonObject io = entry.toObject();
        const QString op = stringValue(io, QStringLiteral("op")).toLower();
        const qint64 value = integerValue(io, QStringLiteral("value"));
        if (value <= 0) {
            continue;
        }
        if (op == QLatin1String("read")) {
            exactRead += quint64(value);
        } else if (op == QLatin1String("write")) {
            exactWrite += quint64(value);
        } else if (op == QLatin1String("sync")) {
            syncBytes += quint64(value);
        } else if (op == QLatin1String("async")) {
            asyncBytes += quint64(value);
        }
    }

    if (exactRead == 0 && exactWrite == 0 && (syncBytes > 0 || asyncBytes > 0)) {
        // 老引擎只给 sync/async：无法区分方向时按总量归入 read，避免编造数据
        readBytes = syncBytes + asyncBytes;
        writeBytes = 0;
        return;
    }
    readBytes = exactRead;
    writeBytes = exactWrite;
}

} // namespace

std::optional<DockerStatsDTO> DockerStatsDTO::fromJson(const QJsonObject &object, QString *error)
{
    DockerStatsDTO dto;

    dto.containerId = stringValue(object, QStringLiteral("id"));
    const QJsonObject cpuStats = object.value(QStringLiteral("cpu_stats")).toObject();
    const QJsonObject preCpuStats = object.value(QStringLiteral("precpu_stats")).toObject();
    if (cpuStats.isEmpty()) {
        if (error) {
            *error = QStringLiteral("stats payload without cpu_stats");
        }
        return std::nullopt;
    }

    dto.cpuTotalUsage = nestedCounter(cpuStats, QStringLiteral("cpu_usage"), QStringLiteral("total_usage"));
    dto.cpuPreTotalUsage = nestedCounter(preCpuStats, QStringLiteral("cpu_usage"), QStringLiteral("total_usage"));
    dto.systemCpuUsage = quint64(qMax<qint64>(0, integerValue(cpuStats, QStringLiteral("system_cpu_usage"))));
    dto.systemPreCpuUsage = quint64(qMax<qint64>(0, integerValue(preCpuStats, QStringLiteral("system_cpu_usage"))));

    dto.onlineCpus = intValue(cpuStats, QStringLiteral("online_cpus"));
    if (dto.onlineCpus <= 0) {
        // 老引擎没有 online_cpus：用 percpu_usage 的条目数兜底
        dto.onlineCpus = cpuStats.value(QStringLiteral("cpu_usage")).toObject().value(QStringLiteral("percpu_usage")).toArray().size();
    }

    const QJsonObject memoryStats = object.value(QStringLiteral("memory_stats")).toObject();
    dto.memoryUsageBytes = quint64(qMax<qint64>(0, integerValue(memoryStats, QStringLiteral("usage"))));
    dto.memoryLimitBytes = quint64(qMax<qint64>(0, integerValue(memoryStats, QStringLiteral("limit"))));
    dto.memoryCacheBytes = parseMemoryCacheBytes(memoryStats);

    dto.networkRxBytes = networkBytes(object, QStringLiteral("rx_bytes"));
    dto.networkTxBytes = networkBytes(object, QStringLiteral("tx_bytes"));
    blockIoBytes(object, dto.blockReadBytes, dto.blockWriteBytes);

    dto.pids = intValue(object.value(QStringLiteral("pids_stats")).toObject(), QStringLiteral("current"));

    return dto;
}

std::optional<DockerStatsDTO> DockerStatsDTO::fromPayload(const QByteArray &payload, QString *error)
{
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (!document.isObject()) {
        if (error) {
            *error = QStringLiteral("stats payload is not a JSON object");
        }
        return std::nullopt;
    }
    return fromJson(document.object(), error);
}

ContainerStats containerStatsFromDto(const DockerStatsDTO &dto)
{
    ContainerStats stats;
    stats.containerId = dto.containerId;
    stats.timestamp = QDateTime::currentDateTimeUtc();
    stats.cpuTotalUsage = dto.cpuTotalUsage;
    stats.cpuPreTotalUsage = dto.cpuPreTotalUsage;
    stats.systemCpuUsage = dto.systemCpuUsage;
    stats.systemPreCpuUsage = dto.systemPreCpuUsage;
    stats.onlineCpus = dto.onlineCpus;
    stats.memoryUsageBytes = dto.memoryUsageBytes;
    stats.memoryCacheBytes = dto.memoryCacheBytes;
    stats.memoryLimitBytes = dto.memoryLimitBytes;
    stats.networkRxBytes = dto.networkRxBytes;
    stats.networkTxBytes = dto.networkTxBytes;
    stats.blockReadBytes = dto.blockReadBytes;
    stats.blockWriteBytes = dto.blockWriteBytes;
    stats.pids = dto.pids;
    return stats;
}

} // namespace Kontainer
