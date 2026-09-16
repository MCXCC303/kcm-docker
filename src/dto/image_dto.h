/*
    SPDX-FileCopyrightText: 2026 kontainer developers
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
 * `GET /images/json` 的单条记录（ARCH_V1 §40）。
 *
 * RepoTags / RepoDigests 可能为 null（中间层镜像），Size 在新旧引擎上可能是
 * 数字或字符串，Containers 为 -1 表示没有容器使用该镜像 —— 全部按可选字段处理。
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

/*! DTO → domain object（方向：dto → domain）。 */
Image imageFromDto(const DockerImageDTO &dto);
QList<Image> imagesFromDto(const QList<DockerImageDTO> &dtos);

} // namespace Kontainer
