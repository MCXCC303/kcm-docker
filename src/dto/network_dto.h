/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "domain/network.h"

#include <QJsonObject>
#include <QList>
#include <QString>

#include <optional>

namespace Kontainer
{

/*!
 * One record of `GET /networks` (ARCH_V5_V8 §3.2).
 *
 * Measured (re-checked in appendix A.3): `/networks` already returns **complete objects** — IPAM,
 * Options, Labels and Containers are all included — so the detail page needs no extra
 * `/networks/{id}` call (one round trip less, and no list-vs-detail intermediate state).
 *
 * All fields are optional: drivers differ a lot (`host`/`none` have no IPAM, overlay has `Peers`,
 * swarm has `Ingress`…), so anything missing falls back to its default.
 */
struct DockerNetworkDTO {
    QString id;
    QString name;
    QString driver;
    QString scope;
    QString created; /*!< RFC3339, parsed by domain */
    bool internal = false;
    bool attachable = false;
    bool ingress = false;
    QList<QPair<QString, QString>> ipamConfigs;
    QList<QPair<QString, QString>> options;
    QList<QPair<QString, QString>> labels;
    QList<NetworkMember> members;

    static std::optional<DockerNetworkDTO> fromJson(const QJsonObject &object, QString *error = nullptr);
    static QList<DockerNetworkDTO> listFromJson(const QByteArray &payload, QString *error = nullptr, int *skipped = nullptr);
};

/*! DTO → domain object (direction: dto → domain). */
Network networkFromDto(const DockerNetworkDTO &dto);
QList<Network> networksFromDto(const QList<DockerNetworkDTO> &dtos);

} // namespace Kontainer
