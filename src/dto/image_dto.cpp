/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "dto/image_dto.h"

#include "dto/json_helpers.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTimeZone>

namespace Kontainer
{

using namespace JsonHelpers;

std::optional<DockerImageDTO> DockerImageDTO::fromJson(const QJsonObject &object, QString *error)
{
    DockerImageDTO dto;

    dto.id = stringValue(object, QStringLiteral("Id"));
    if (dto.id.isEmpty()) {
        if (error) {
            *error = QStringLiteral("image entry without Id");
        }
        return std::nullopt;
    }

    dto.repoTags = stringListValue(object, QStringLiteral("RepoTags"));
    dto.repoDigests = stringListValue(object, QStringLiteral("RepoDigests"));
    dto.sizeBytes = integerValue(object, QStringLiteral("Size"));
    dto.createdUnix = integerValue(object, QStringLiteral("Created"));
    if (object.contains(QStringLiteral("Containers"))) {
        dto.containers = intValue(object, QStringLiteral("Containers"), -1);
    }

    return dto;
}

QList<DockerImageDTO> DockerImageDTO::listFromJson(const QByteArray &payload, QString *error, int *skipped)
{
    QList<DockerImageDTO> result;

    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (!document.isArray()) {
        if (error) {
            *error = QStringLiteral("image list payload is not a JSON array");
        }
        return result;
    }

    int skippedCount = 0;
    const QJsonArray array = document.array();
    result.reserve(array.size());
    for (const QJsonValue &entry : array) {
        if (!entry.isObject()) {
            ++skippedCount;
            continue;
        }
        QString entryError;
        const auto dto = DockerImageDTO::fromJson(entry.toObject(), &entryError);
        if (dto.has_value()) {
            result.append(*dto);
        } else {
            ++skippedCount;
        }
    }

    if (skipped) {
        *skipped = skippedCount;
    }
    return result;
}

Image imageFromDto(const DockerImageDTO &dto)
{
    Image image;
    image.id = dto.id;
    image.repoTags = dto.repoTags;
    image.repoDigests = dto.repoDigests;
    image.sizeBytes = dto.sizeBytes;
    image.created = QDateTime::fromSecsSinceEpoch(dto.createdUnix, QTimeZone::UTC);
    image.containerCount = dto.containers;
    image.inUse = dto.containers > 0;
    return image;
}

QList<Image> imagesFromDto(const QList<DockerImageDTO> &dtos)
{
    QList<Image> images;
    images.reserve(dtos.size());
    for (const DockerImageDTO &dto : dtos) {
        images.append(imageFromDto(dto));
    }
    return images;
}

} // namespace Kontainer
