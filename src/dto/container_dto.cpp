/*
    SPDX-FileCopyrightText: 2026 kontainer developers
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

    // Names 允许缺失（§41 宽容解析）：此时用短 ID 兜底，宁可显示得少一点，
    // 也不能因为一个可选字段丢失就整条记录不显示
    const QStringList names = stringListValue(object, QStringLiteral("Names"));
    if (names.isEmpty()) {
        dto.name = dto.id.left(12);
    } else {
        // Docker 返回的容器名带有前导 '/'，在此处去掉，domain model 保持干净
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

    // 网络成员：容器列表里就有 `NetworkSettings.Networks`，与 inspect 的载荷形状一致，
    // 因此复用同一个解析器（不要写第二份）
    dto.networks = parseNetworks(object.value(QStringLiteral("NetworkSettings")).toObject());

    const QJsonValue ports = object.value(QStringLiteral("Ports"));
    if (ports.isArray()) {
        const QJsonArray array = ports.toArray();
        dto.ports.reserve(array.size());
        for (const QJsonValue &entry : array) {
            if (!entry.isObject()) {
                continue; // 未知/损坏的端口条目直接忽略
            }
            const QJsonObject portObject = entry.toObject();
            DockerPortDTO port;
            port.ip = stringValue(portObject, QStringLiteral("IP"));
            // 端口号必须落在 quint16 范围内：超范围的值直接丢弃，避免回绕成错误端口
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
