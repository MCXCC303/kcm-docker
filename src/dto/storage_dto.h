/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
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
 * DTO for `GET /system/df`.
 *
 * Newer engines report per-category totals (ImageUsage/ContainerUsage/VolumeUsage/BuildCacheUsage)
 * while older ones only have the detail arrays, so both are kept and the mapping stage decides
 * which to prefer (§24: structured API).
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

/*! DTO → domain (§26). */
StorageUsage storageUsageFromDto(const DockerStorageDTO &dto);

} // namespace Kontainer
