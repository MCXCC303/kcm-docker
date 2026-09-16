/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "domain/storage_usage.h"

#include <QJsonObject>
#include <QString>

#include <optional>

namespace Kontainer
{

/*!
 * `GET /system/df` 的 DTO。
 *
 * 新版引擎直接给出各类别的用量合计（ImageUsage/ContainerUsage/VolumeUsage/BuildCacheUsage），
 * 老版本只有明细数组，因此这里两者都保留，由映射阶段决定优先使用哪个（§24：结构化 API）。
 */
struct DockerStorageDTO {
    qint64 layersSize = -1;
    qint64 imageUsage = -1;
    qint64 containerUsage = -1;
    qint64 volumeUsage = -1;
    qint64 buildCacheUsage = -1;
    bool buildCachePresent = false;

    int imageCount = 0;
    int containerCount = 0;
    int volumeCount = 0;
    int buildCacheCount = 0;

    qint64 imagesSumFromDetails = -1;
    qint64 containersSumFromDetails = -1;
    qint64 volumesSumFromDetails = -1;
    qint64 buildCacheSumFromDetails = -1;

    static std::optional<DockerStorageDTO> fromJson(const QJsonObject &object, QString *error = nullptr);
    static std::optional<DockerStorageDTO> fromPayload(const QByteArray &payload, QString *error = nullptr);
};

/*! DTO → domain（§26）。 */
StorageUsage storageUsageFromDto(const DockerStorageDTO &dto);

} // namespace Kontainer
