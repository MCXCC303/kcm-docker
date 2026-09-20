/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "domain/image_detail.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>

namespace Kontainer
{

/*! DTO for `GET /images/{id}/json`. */
struct DockerImageInspectDTO {
    QString id;
    QStringList repoTags;
    QStringList repoDigests;
    QString created; /*!< raw ISO string */
    qint64 sizeBytes = 0;
    QString architecture;
    QString os;
    QString variant;
    QString author;
    QStringList layers;
    QStringList environment;
    QStringList entrypoint;
    QStringList command;
    QString workingDirectory;

    static std::optional<DockerImageInspectDTO> fromJson(const QJsonObject &object, QString *error = nullptr);
    static std::optional<DockerImageInspectDTO> fromPayload(const QByteArray &payload, QString *error = nullptr);
};

/*! DTO → domain (§26). */
ImageDetail imageDetailFromDto(const DockerImageInspectDTO &dto);

} // namespace Kontainer
