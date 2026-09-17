/*
    SPDX-FileCopyrightText: 2026 kontainer developers
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
 * `GET /volumes` 的单条记录（ARCH_V5_V8 §3.5）。
 *
 * 注意载荷形态与 `/networks` **不同**：这里是**对象**（`{Volumes: [...] | null, Warnings: [...]}`），
 * 空列表时 `Volumes` 会是 `null`（本机实测如此），因此解析要同时接受 null 与缺失。
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
    /*! 解析整个响应体；`Warnings` 里的内容透出给调用方（引擎的提醒不该被丢掉）。 */
    static QList<DockerVolumeDTO> listFromPayload(const QByteArray &payload,
                                                  QStringList *warnings = nullptr,
                                                  QString *error = nullptr,
                                                  int *skipped = nullptr);
};

/*! DTO → domain object（方向：dto → domain）。 */
Volume volumeFromDto(const DockerVolumeDTO &dto);
QList<Volume> volumesFromDto(const QList<DockerVolumeDTO> &dtos);

} // namespace Kontainer
