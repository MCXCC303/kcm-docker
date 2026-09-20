/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "domain/volume.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace Kontainer
{

/*!
 * One `GET /volumes` record (ARCH_V5_V8 §3.5).
 *
 * Payload shape differs from `/networks`: an **object** (`{Volumes: [...] | null, Warnings: [...]}`), with
 * `Volumes` = `null` when empty (observed locally), so parsing accepts null and missing.
 */
struct DockerVolumeDTO {
    QString name;
    QString driver;
    QString mountpoint;
    QString createdAt;
    QString scope;
    QString status;
    QList<QPair<QString, QString>> labels;
    QList<QPair<QString, QString>> options;
    qint64 sizeBytes = -1;
    int refCount = -1;

    static std::optional<DockerVolumeDTO> fromJson(const QJsonObject &object, QString *error = nullptr);
    /*! Parse the whole payload; `Warnings` is handed to the caller (engine warnings matter). */
    static QList<DockerVolumeDTO> listFromPayload(const QByteArray &payload,
                                                  QStringList *warnings = nullptr,
                                                  QString *error = nullptr,
                                                  int *skipped = nullptr);
};

/*! DTO -> domain object. */
Volume volumeFromDto(const DockerVolumeDTO &dto);
QList<Volume> volumesFromDto(const QList<DockerVolumeDTO> &dtos);

} // namespace Kontainer
