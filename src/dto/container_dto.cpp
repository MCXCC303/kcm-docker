/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "dto/container_dto.h"

#include "dto/container_network_dto.h"

#include "dto/json_helpers.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTimeZone>

namespace Kontainer
{

using namespace JsonHelpers;

std::optional<DockerContainerDTO> DockerContainerDTO::fromJson(const QJsonObject &object, QString *error)
{
    DockerContainerDTO dto;

    dto.id = stringValue(object, QStringLiteral("Id"));
    if (dto.id.isEmpty()) {
        if (error) {
            *error = QStringLiteral("container entry without Id");
        }
        return std::nullopt;
    }

    dto.state = stringValue(object, QStringLiteral("State"));
    if (dto.state.isEmpty()) {
        if (error) {
            *error = QStringLiteral("container %1 without State").arg(dto.id);
        }
        return std::nullopt;
    }

    // Names may be absent (§41 lenient parsing): fall back to the short ID rather than dropping
    // the whole record because one optional field is gone
    const QStringList names = stringListValue(object, QStringLiteral("Names"));
    if (names.isEmpty()) {
        dto.name = dto.id.left(12);
    } else {
        // Docker returns container names with a leading '/'; strip it to keep the domain model clean
        dto.name = names.first();
        if (dto.name.startsWith(QLatin1Char('/'))) {
            dto.name.remove(0, 1);
        }
    }

    dto.image = stringValue(object, QStringLiteral("Image"));
    dto.imageId = stringValue(object, QStringLiteral("ImageID"));
    dto.status = stringValue(object, QStringLiteral("Status"));
    dto.createdUnix = integerValue(object, QStringLiteral("Created"));

    const QJsonValue health = object.value(QStringLiteral("Health"));
    if (health.isObject()) {
        dto.healthStatus = stringValue(health.toObject(), QStringLiteral("Status"));
    }

    // Network membership: the list payload already carries `NetworkSettings.Networks` in the same
    // shape as inspect, so reuse the same parser instead of writing a second one
    dto.networks = parseNetworks(object.value(QStringLiteral("NetworkSettings")).toObject());

    const QJsonValue ports = object.value(QStringLiteral("Ports"));
    if (ports.isArray()) {
        const QJsonArray array = ports.toArray();
        dto.ports.reserve(array.size());
        for (const QJsonValue &entry : array) {
            if (!entry.isObject()) {
                continue; // ignore unknown/broken port entries
            }
            const QJsonObject portObject = entry.toObject();
            DockerPortDTO port;
            port.ip = stringValue(portObject, QStringLiteral("IP"));
            // Port numbers must fit quint16: out-of-range values are dropped, never wrapped
            const auto clampPort = [](qint64 value) -> quint16 {
                return (value > 0 && value <= 65535) ? static_cast<quint16>(value) : 0;
            };
            port.privatePort = clampPort(integerValue(portObject, QStringLiteral("PrivatePort")));
            port.publicPort = clampPort(integerValue(portObject, QStringLiteral("PublicPort")));
            port.type = stringValue(portObject, QStringLiteral("Type"));
            dto.ports.append(port);
        }
    }

    return dto;
}

QList<DockerContainerDTO> DockerContainerDTO::listFromJson(const QByteArray &payload, QString *error, int *skipped)
{
    QList<DockerContainerDTO> result;

    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (!document.isArray()) {
        if (error) {
            *error = QStringLiteral("container list payload is not a JSON array");
        }
        return result;
    }

    int skippedCount = 0;
    const QJsonArray array = document.array();
    result.reserve(array.size());
    for (const QJsonValue &entry : array) {
        if (!entry.isObject()) {
            ++skippedCount;
            continue;
        }
        QString entryError;
        const auto dto = DockerContainerDTO::fromJson(entry.toObject(), &entryError);
        if (dto.has_value()) {
            result.append(*dto);
        } else {
            ++skippedCount;
        }
    }

    if (skipped) {
        *skipped = skippedCount;
    }
    return result;
}

Container containerFromDto(const DockerContainerDTO &dto)
{
    Container container;
    container.id = dto.id;
    container.name = dto.name;
    container.image = dto.image;
    container.imageId = dto.imageId;
    container.status = dto.status;
    container.state = containerStateFromString(dto.state);
    container.health = healthStateFromString(dto.healthStatus);
    container.created = QDateTime::fromSecsSinceEpoch(dto.createdUnix, QTimeZone::UTC);

    container.ports.reserve(dto.ports.size());
    for (const DockerPortDTO &portDto : dto.ports) {
        Port port;
        port.ip = portDto.ip;
        port.privatePort = portDto.privatePort;
        port.publicPort = portDto.publicPort;
        port.type = portDto.type;
        container.ports.append(port);
    }

    container.networks.reserve(dto.networks.size());
    for (const ContainerNetworkDTO &networkDto : dto.networks) {
        ContainerNetwork network;
        network.name = networkDto.name;
        network.id = networkDto.networkId;
        network.ipAddress = networkDto.ipAddress;
        network.ipv6Address = networkDto.ipv6Address;
        network.macAddress = networkDto.macAddress;
        network.gateway = networkDto.gateway;
        container.networks.append(network);
    }
    return container;
}

QList<Container> containersFromDto(const QList<DockerContainerDTO> &dtos)
{
    QList<Container> containers;
    containers.reserve(dtos.size());
    for (const DockerContainerDTO &dto : dtos) {
        containers.append(containerFromDto(dto));
    }
    return containers;
}

QList<ContainerNetworkDTO> parseNetworks(const QJsonObject &networkSettings)
{
    QList<ContainerNetworkDTO> networks;
    const QJsonObject map = networkSettings.value(QStringLiteral("Networks")).toObject();
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
        if (!it.value().isObject()) {
            continue;
        }
        const QJsonObject entry = it.value().toObject();
        ContainerNetworkDTO network;
        network.name = it.key();
        network.networkId = stringValue(entry, QStringLiteral("NetworkID"));
        network.ipAddress = stringValue(entry, QStringLiteral("IPAddress"));
        network.ipv6Address = stringValue(entry, QStringLiteral("GlobalIPv6Address"));
        network.macAddress = stringValue(entry, QStringLiteral("MacAddress"));
        network.gateway = stringValue(entry, QStringLiteral("Gateway"));
        networks.append(network);
    }
    return networks;
}

} // namespace Kontainer
