/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "dto/image_inspect_dto.h"

#include "dto/json_helpers.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace Kontainer
{

using namespace JsonHelpers;

std::optional<DockerImageInspectDTO> DockerImageInspectDTO::fromJson(const QJsonObject &object, QString *error)
{
    DockerImageInspectDTO dto;

    dto.id = stringValue(object, QStringLiteral("Id"));
    if (dto.id.isEmpty()) {
        if (error) {
            *error = QStringLiteral("image inspect payload without Id");
        }
        return std::nullopt;
    }

    dto.repoTags = stringListValue(object, QStringLiteral("RepoTags"));
    dto.repoDigests = stringListValue(object, QStringLiteral("RepoDigests"));
    dto.created = stringValue(object, QStringLiteral("Created"));
    dto.sizeBytes = integerValue(object, QStringLiteral("Size"));
    dto.architecture = stringValue(object, QStringLiteral("Architecture"));
    dto.os = stringValue(object, QStringLiteral("Os"));
    dto.variant = stringValue(object, QStringLiteral("Variant"));
    dto.author = stringValue(object, QStringLiteral("Author"));

    const QJsonObject rootFs = object.value(QStringLiteral("RootFS")).toObject();
    dto.layers = stringListValue(rootFs, QStringLiteral("Layers"));

    const QJsonObject config = object.value(QStringLiteral("Config")).toObject();
    dto.environment = stringListValue(config, QStringLiteral("Env"));
    dto.entrypoint = stringListValue(config, QStringLiteral("Entrypoint"));
    dto.command = stringListValue(config, QStringLiteral("Cmd"));
    dto.workingDirectory = stringValue(config, QStringLiteral("WorkingDir"));

    return dto;
}

std::optional<DockerImageInspectDTO> DockerImageInspectDTO::fromPayload(const QByteArray &payload, QString *error)
{
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (!document.isObject()) {
        if (error) {
            *error = QStringLiteral("image inspect payload is not a JSON object");
        }
        return std::nullopt;
    }
    return fromJson(document.object(), error);
}

ImageDetail imageDetailFromDto(const DockerImageInspectDTO &dto)
{
    ImageDetail detail;
    detail.id = dto.id;
    detail.repoTags = dto.repoTags;
    detail.repoDigests = dto.repoDigests;
    detail.created = parseDockerTimestamp(dto.created);
    detail.sizeBytes = dto.sizeBytes;
    detail.architecture = dto.architecture;
    detail.os = dto.os;
    detail.variant = dto.variant;
    detail.author = dto.author;
    detail.layers = dto.layers;
    detail.environment = dto.environment;
    detail.entrypoint = dto.entrypoint;
    detail.command = dto.command;
    detail.workingDirectory = dto.workingDirectory;
    return detail;
}

} // namespace Kontainer
