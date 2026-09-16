/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "dto/storage_dto.h"

#include "dto/json_helpers.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace Kontainer
{

using namespace JsonHelpers;

namespace
{

/*! 累加明细数组里的某个数值字段（字段缺失的条目跳过）。 */
qint64 sumDetailField(const QJsonObject &object, const QString &arrayKey, const QString &field, bool *sawArray)
{
    const QJsonValue value = object.value(arrayKey);
    if (!value.isArray()) {
        if (sawArray) {
            *sawArray = false;
        }
        return -1;
    }
    if (sawArray) {
        *sawArray = true;
    }
    qint64 total = 0;
    const QJsonArray array = value.toArray();
    for (const QJsonValue &entry : array) {
        if (!entry.isObject()) {
            continue;
        }
        const qint64 fieldValue = integerValue(entry.toObject(), field);
        if (fieldValue > 0) {
            total += fieldValue;
        }
    }
    return total;
}

/*! Volumes 的用量在嵌套的 UsageData.Size 里。 */
qint64 sumVolumeSizes(const QJsonObject &object, bool *sawArray)
{
    const QJsonValue value = object.value(QStringLiteral("Volumes"));
    if (!value.isArray()) {
        if (sawArray) {
            *sawArray = false;
        }
        return -1;
    }
    if (sawArray) {
        *sawArray = true;
    }
    qint64 total = 0;
    const QJsonArray array = value.toArray();
    for (const QJsonValue &entry : array) {
        if (!entry.isObject()) {
            continue;
        }
        const QJsonObject usageData = entry.toObject().value(QStringLiteral("UsageData")).toObject();
        const qint64 size = integerValue(usageData, QStringLiteral("Size"));
        if (size > 0) {
            total += size;
        }
    }
    return total;
}

} // namespace

std::optional<DockerStorageDTO> DockerStorageDTO::fromJson(const QJsonObject &object, QString *error)
{
    if (object.isEmpty()) {
        if (error) {
            *error = QStringLiteral("system df payload is empty");
        }
        return std::nullopt;
    }

    DockerStorageDTO dto;
    dto.layersSize = integerValue(object, QStringLiteral("LayersSize"), -1);
    dto.imageUsage = integerValue(object, QStringLiteral("ImageUsage"), -1);
    dto.containerUsage = integerValue(object, QStringLiteral("ContainerUsage"), -1);
    dto.volumeUsage = integerValue(object, QStringLiteral("VolumeUsage"), -1);
    dto.buildCacheUsage = integerValue(object, QStringLiteral("BuildCacheUsage"), -1);

    dto.imageCount = object.value(QStringLiteral("Images")).toArray().size();
    dto.containerCount = object.value(QStringLiteral("Containers")).toArray().size();
    dto.volumeCount = object.value(QStringLiteral("Volumes")).toArray().size();
    dto.buildCacheCount = object.value(QStringLiteral("BuildCache")).toArray().size();
    dto.buildCachePresent = object.contains(QStringLiteral("BuildCache"));

    dto.imagesSumFromDetails = sumDetailField(object, QStringLiteral("Images"), QStringLiteral("Size"), nullptr);
    dto.containersSumFromDetails = sumDetailField(object, QStringLiteral("Containers"), QStringLiteral("SizeRw"), nullptr);
    dto.volumesSumFromDetails = sumVolumeSizes(object, nullptr);
    dto.buildCacheSumFromDetails = sumDetailField(object, QStringLiteral("BuildCache"), QStringLiteral("Size"), nullptr);

    return dto;
}

std::optional<DockerStorageDTO> DockerStorageDTO::fromPayload(const QByteArray &payload, QString *error)
{
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (!document.isObject()) {
        if (error) {
            *error = QStringLiteral("system df payload is not a JSON object");
        }
        return std::nullopt;
    }
    return fromJson(document.object(), error);
}

StorageUsage storageUsageFromDto(const DockerStorageDTO &dto)
{
    StorageUsage usage;
    usage.valid = true;
    usage.buildCacheAvailable = dto.buildCachePresent || dto.buildCacheUsage >= 0;

    // 优先使用引擎给出的类别合计；缺失时退回明细求和（§24）
    const auto pick = [](qint64 primary, qint64 fallback) -> qint64 {
        if (primary >= 0) {
            return primary;
        }
        return fallback >= 0 ? fallback : -1;
    };

    usage.imagesBytes = pick(dto.imageUsage, dto.imagesSumFromDetails);
    usage.containersBytes = pick(dto.containerUsage, dto.containersSumFromDetails);
    usage.volumesBytes = pick(dto.volumeUsage, dto.volumesSumFromDetails);
    usage.buildCacheBytes = pick(dto.buildCacheUsage, dto.buildCacheSumFromDetails);
    usage.layersBytes = dto.layersSize;

    usage.imageCount = dto.imageCount;
    usage.containerCount = dto.containerCount;
    usage.volumeCount = dto.volumeCount;
    usage.buildCacheCount = dto.buildCacheCount;
    return usage;
}

} // namespace Kontainer
