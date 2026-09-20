/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

namespace Kontainer
{

/*!
 * `NetworkSettings.Networks.<name>`: which network a container is attached to.
 *
 * Do not confuse with `DockerNetworkDTO` in `dto/network_dto.h` (a **network object** from
 * `GET /networks`): the fields are entirely different. It lives in its own header because both the
 * container **list** and container **inspect** need it, and those two DTO headers include each
 * other.
 */
struct ContainerNetworkDTO {
    QString name;
    QString networkId;
    QString ipAddress;
    QString ipv6Address;
    QString macAddress;
    QString gateway;
};

/*! Parse `NetworkSettings.Networks` (list and inspect share the payload shape and this implementation). */
QList<ContainerNetworkDTO> parseNetworks(const QJsonObject &networkSettings);

} // namespace Kontainer
