/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "domain/container.h"
#include "dto/container_network_dto.h"

#include <QJsonObject>
#include <QList>
#include <QString>

#include <optional>

namespace Kontainer
{

/*! Docker Engine API port mapping entry (/containers/json → Ports[]). */
struct DockerPortDTO {
    QString ip;
    quint16 privatePort = 0;
    quint16 publicPort = 0; /*!< 0 means not published to the host */
    QString type; /*!< tcp / udp / sctp */
};

/*!
 * One **declared** binding from `HostConfig.PortBindings`.
 *
 * Deliberately separate from `DockerPortDTO` (from `NetworkSettings.Ports`, the actual
 * publication): a declared `HostPort` may be a range string (`"47300-47309"`), and an entry that
 * fails to parse is dropped entirely.
 */
struct DockerDeclaredPortDTO {
    quint16 containerPort = 0;
    QString protocol;
    QString hostIp;
    quint16 hostPort = 0;
    quint16 hostPortEnd = 0;
};

/*!
 * One record of `GET /containers/json` (ARCH_V1 §40: a DTO mirrors the API schema).
 *
 * Only Id / State / Names are required; missing or mistyped fields fall back to defaults, so one
 * broken record cannot fail the whole list.
 */
struct DockerContainerDTO {
    QString id;
    QString name;
    QString image;
    QString imageId;
    QString state;
    QString status;
    QString healthStatus; /*!< Health.Status on Docker 29+; empty on older engines */
    qint64 createdUnix = 0;
    QList<DockerPortDTO> ports;
    /*! `NetworkSettings.Networks` (only reliable source of network membership; see domain/container.h). */
    QList<ContainerNetworkDTO> networks;

    /*! Parse one record; nullopt plus error when a required field is missing. */
    static std::optional<DockerContainerDTO> fromJson(const QJsonObject &object, QString *error = nullptr);

    /*!
     * Parse the full `/containers/json` response.
     * Invalid JSON or not an array → empty list plus error (caller maps to UnexpectedPayload).
     * One broken record → skipped, skipped counter bumped, the rest returned as usual.
     */
    static QList<DockerContainerDTO> listFromJson(const QByteArray &payload, QString *error = nullptr, int *skipped = nullptr);
};

/*!
 * DTO → domain mapping lives in the DTO layer (ARCH_V2 §26): the direction is always
 * dto → domain, and domain never depends back on the API schema.
 */
Container containerFromDto(const DockerContainerDTO &dto);
QList<Container> containersFromDto(const QList<DockerContainerDTO> &dtos);

} // namespace Kontainer
