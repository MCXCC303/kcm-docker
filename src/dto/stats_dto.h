/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "domain/container_stats.h"

#include <QJsonObject>
#include <QString>

#include <optional>

namespace Kontainer
{

/*!
 * `GET /containers/{id}/stats?stream=false` 的 DTO。
 *
 * Docker 只给出累加计数器与原始内存数据；百分比与速率都不在这里计算（§17.2/§18）。
 */
struct DockerStatsDTO {
    QString containerId;

    quint64 cpuTotalUsage = 0;
    quint64 cpuPreTotalUsage = 0;
    quint64 systemCpuUsage = 0;
    quint64 systemPreCpuUsage = 0;
    int onlineCpus = 0;

    quint64 memoryUsageBytes = 0;
    quint64 memoryCacheBytes = 0;
    quint64 memoryLimitBytes = 0;

    quint64 networkRxBytes = 0;
    quint64 networkTxBytes = 0;
    quint64 blockReadBytes = 0;
    quint64 blockWriteBytes = 0;
    int pids = 0;

    static std::optional<DockerStatsDTO> fromJson(const QJsonObject &object, QString *error = nullptr);
    static std::optional<DockerStatsDTO> fromPayload(const QByteArray &payload, QString *error = nullptr);
};

/*! DTO → domain（§26）。 */
ContainerStats containerStatsFromDto(const DockerStatsDTO &dto);

} // namespace Kontainer
