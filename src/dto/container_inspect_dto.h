/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/docker_error.h"
#include "domain/container_detail.h"
#include "dto/container_dto.h"
#include "dto/container_network_dto.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace Kontainer
{

/*! inspect → Mounts[]. */
struct DockerMountDTO {
    QString type;
    /*! Named volume name (Mounts[].Name); empty for bind and anonymous volumes. */
    QString name;
    QString source;
    QString destination;
    QString mode;
    bool readWrite = true;
};

/*!
 * DTO for `GET /containers/{id}/json` (ARCH_V2 §26: a DTO mirrors the API schema).
 *
 * Only fields the UI needs; timestamps stay the raw ISO strings Docker returns and are parsed
 * once when mapping to domain (including nanosecond-precision handling).
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
    QStringList labels; /*!< raw "key=value" */
    QString workingDirectory;
    QString user;
    QString hostname;
    /*! `Config.Tty`: picks the log stream format (TTY = raw bytes, non-TTY = stdcopy frames). */
    bool tty = false;
    QString restartPolicy;

    QDateTime created;

    QList<DockerPortDTO> ports;
    /*! `HostConfig.PortBindings`: bindings the container **declares** (vs the published `ports`). */
    QList<DockerDeclaredPortDTO> declaredPorts;
    QList<ContainerNetworkDTO> networks;
    QList<DockerMountDTO> mounts;

    static std::optional<DockerContainerInspectDTO> fromJson(const QJsonObject &object, QString *error = nullptr);
    static std::optional<DockerContainerInspectDTO> fromPayload(const QByteArray &payload, QString *error = nullptr);
};

/*! DTO → domain (§26: the direction is dto → domain). */
ContainerDetail containerDetailFromDto(const DockerContainerInspectDTO &dto);

} // namespace Kontainer
