/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/docker_error.h"
#include "domain/container_detail.h"
#include "dto/container_dto.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace Kontainer
{

/*! inspect → NetworkSettings.Networks.<name>。 */
struct DockerNetworkDTO {
    QString name;
    QString networkId;
    QString ipAddress;
    QString ipv6Address;
    QString macAddress;
    QString gateway;
};

/*! inspect → Mounts[]。 */
struct DockerMountDTO {
    QString type;
    QString source;
    QString destination;
    QString mode;
    bool readWrite = true;
};

/*!
 * `GET /containers/{id}/json` 的 DTO（ARCH_V2 §26：DTO 镜像 API schema）。
 *
 * 只提取界面需要的字段；时间保持 Docker 返回的原始 ISO 字符串，
 * 在映射到 domain 时统一解析（含纳秒精度的兼容处理）。
 */
struct DockerContainerInspectDTO {
    QString id;
    QString name;
    QString image;
    QString imageId;
    QString platform;

    /* State */
    QString status;
    bool running = false;
    bool paused = false;
    bool restarting = false;
    bool dead = false;
    bool oomKilled = false;
    int exitCode = 0;
    int pid = 0;
    int restartCount = 0;
    QString startedAt;
    QString finishedAt;
    QString healthStatus;

    /* Config */
    QStringList environment;
    QStringList command;
    QStringList entrypoint;
    QStringList labels; /*!< 原始 "key=value" */
    QString workingDirectory;
    QString user;
    QString hostname;
    QString restartPolicy;

    QDateTime created;

    QList<DockerPortDTO> ports;
    QList<DockerNetworkDTO> networks;
    QList<DockerMountDTO> mounts;

    static std::optional<DockerContainerInspectDTO> fromJson(const QJsonObject &object, QString *error = nullptr);
    static std::optional<DockerContainerInspectDTO> fromPayload(const QByteArray &payload, QString *error = nullptr);
};

/*! DTO → domain（§26：方向 dto → domain）。 */
ContainerDetail containerDetailFromDto(const DockerContainerInspectDTO &dto);

} // namespace Kontainer
