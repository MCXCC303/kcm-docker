/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "domain/image.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace Kontainer
{

/*!
 * One record of `GET /images/json` (ARCH_V1 §40).
 *
 * RepoTags / RepoDigests may be null (intermediate layers), Size may be a number or a string
 * depending on the engine, and Containers = -1 means no container uses the image — so all of them
 * are treated as optional fields.
 */
struct DockerImageDTO {
    QString id;
    QStringList repoTags;
    QStringList repoDigests;
    qint64 sizeBytes = 0;
    qint64 createdUnix = 0;
    int containers = -1;

    static std::optional<DockerImageDTO> fromJson(const QJsonObject &object, QString *error = nullptr);
    static QList<DockerImageDTO> listFromJson(const QByteArray &payload, QString *error = nullptr, int *skipped = nullptr);
};

/*! DTO → domain object (direction: dto → domain). */
Image imageFromDto(const DockerImageDTO &dto);
QList<Image> imagesFromDto(const QList<DockerImageDTO> &dtos);

} // namespace Kontainer
