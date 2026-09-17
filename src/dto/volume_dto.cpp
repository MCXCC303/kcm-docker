/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "dto/volume_dto.h"

#include "dto/json_helpers.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>

#include <algorithm>

namespace Kontainer
{

using namespace JsonHelpers;

namespace
{
/*! 对象形式（`Labels` / `Options`）→ 按键排序的键值对列表（顺序稳定）。 */
QList<QPair<QString, QString>> pairsFromObject(const QJsonValue &value)
{
    QList<QPair<QString, QString>> pairs;
    if (!value.isObject()) {
        return pairs;
    }
    const QJsonObject object = value.toObject();
    pairs.reserve(object.size());
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        const QJsonValue entry = it.value();
        QString text;
        if (entry.isString()) {
            text = entry.toString();
        } else if (entry.isBool()) {
            text = entry.toBool() ? QStringLiteral("true") : QStringLiteral("false");
        } else if (entry.isDouble()) {
            text = QString::number(entry.toDouble());
        }
        pairs.append({it.key(), text});
    }
    std::sort(pairs.begin(), pairs.end(), [](const auto &lhs, const auto &rhs) {
        return lhs.first < rhs.first;
    });
    return pairs;
}
} // namespace

std::optional<DockerVolumeDTO> DockerVolumeDTO::fromJson(const QJsonObject &object, QString *error)
{
    DockerVolumeDTO dto;

    dto.name = stringValue(object, QStringLiteral("Name"));
    if (dto.name.isEmpty()) {
        if (error) {
            *error = QStringLiteral("volume entry without Name");
        }
        return std::nullopt;
    }

    dto.driver = stringValue(object, QStringLiteral("Driver"));
    dto.mountpoint = stringValue(object, QStringLiteral("Mountpoint"));
    dto.createdAt = stringValue(object, QStringLiteral("CreatedAt"));
    dto.scope = stringValue(object, QStringLiteral("Scope"));
    dto.status = object.value(QStringLiteral("Status")).toObject().value(QStringLiteral("Text")).toString();
    if (dto.status.isEmpty()) {
        // 有的引擎把 Status 直接写成字符串
        dto.status = stringValue(object, QStringLiteral("Status"));
    }
    dto.labels = pairsFromObject(object.value(QStringLiteral("Labels")));
    dto.options = pairsFromObject(object.value(QStringLiteral("Options")));

    // UsageData 是可选的（引擎可能没扫、也可能被关掉）：缺失就是"未知"（-1）
    const QJsonValue usage = object.value(QStringLiteral("UsageData"));
    if (usage.isObject()) {
        const QJsonObject usageObject = usage.toObject();
        if (usageObject.contains(QStringLiteral("Size"))) {
            dto.sizeBytes = integerValue(usageObject, QStringLiteral("Size"), -1);
        }
        if (usageObject.contains(QStringLiteral("RefCount"))) {
            dto.refCount = intValue(usageObject, QStringLiteral("RefCount"), -1);
        }
    }

    return dto;
}

QList<DockerVolumeDTO> DockerVolumeDTO::listFromPayload(const QByteArray &payload,
                                                        QStringList *warnings,
                                                        QString *error,
                                                        int *skipped)
{
    QList<DockerVolumeDTO> result;

    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (!document.isObject()) {
        if (error) {
            *error = QStringLiteral("volume list payload is not a JSON object");
        }
        return result;
    }

    const QJsonObject root = document.object();
    if (warnings) {
        *warnings = stringListValue(root, QStringLiteral("Warnings"));
    }

    // 空列表时 `Volumes` 是 null（本机实测）：这不是错误，返回空即可
    const QJsonValue volumesValue = root.value(QStringLiteral("Volumes"));
    if (volumesValue.isNull() || volumesValue.isUndefined()) {
        return result;
    }
    if (!volumesValue.isArray()) {
        if (error) {
            *error = QStringLiteral("volume list payload has no Volumes array");
        }
        return result;
    }

    int skippedCount = 0;
    const QJsonArray array = volumesValue.toArray();
    result.reserve(array.size());
    for (const QJsonValue &entry : array) {
        if (!entry.isObject()) {
            ++skippedCount;
            continue;
        }
        const auto dto = DockerVolumeDTO::fromJson(entry.toObject(), nullptr);
        if (!dto) {
            ++skippedCount;
            continue;
        }
        result.append(*dto);
    }

    if (skipped) {
        *skipped = skippedCount;
    }
    return result;
}

Volume volumeFromDto(const DockerVolumeDTO &dto)
{
    Volume volume;
    volume.name = dto.name;
    volume.driver = dto.driver.isEmpty() ? QStringLiteral("local") : dto.driver;
    volume.mountpoint = dto.mountpoint;
    volume.createdAt = parseDockerTimestamp(dto.createdAt);
    volume.scope = dto.scope;
    volume.labels = dto.labels;
    volume.options = dto.options;
    volume.sizeBytes = dto.sizeBytes;
    volume.refCount = dto.refCount;
    volume.status = dto.status;
    return volume;
}

QList<Volume> volumesFromDto(const QList<DockerVolumeDTO> &dtos)
{
    QList<Volume> volumes;
    volumes.reserve(dtos.size());
    for (const DockerVolumeDTO &dto : dtos) {
        volumes.append(volumeFromDto(dto));
    }
    return volumes;
}

} // namespace Kontainer
