/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "dto/network_dto.h"

#include "dto/json_helpers.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <algorithm>

namespace Kontainer
{

using namespace JsonHelpers;

namespace
{
/*! Object form (`Labels` / `Options`) → key-sorted pair list (stable order, no UI jumping). */
QList<QPair<QString, QString>> pairsFromObject(const QJsonValue &value)
{
    QList<QPair<QString, QString>> pairs;
    if (!value.isObject()) {
        return pairs;
    }
    const QJsonObject object = value.toObject();
    pairs.reserve(object.size());
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        // Values should be strings, but driver options do contain numbers/booleans: stringify
        const QJsonValue entry = it.value();
        QString text;
        if (entry.isString()) {
            text = entry.toString();
        } else if (entry.isBool()) {
            text = entry.toBool() ? QStringLiteral("true") : QStringLiteral("false");
        } else if (entry.isDouble()) {
            text = QString::number(entry.toDouble());
        }
        pairs.append({it.key(), text});
    }
    std::sort(pairs.begin(), pairs.end(), [](const auto &lhs, const auto &rhs) {
        return lhs.first < rhs.first;
    });
    return pairs;
}

/*! `172.18.0.2/16` → `172.18.0.2` (the UI hides the prefix; the subnet gets its own row). */
QString addressWithoutPrefix(const QString &value)
{
    const int slash = value.indexOf(QLatin1Char('/'));
    return slash > 0 ? value.left(slash) : value;
}
} // namespace

std::optional<DockerNetworkDTO> DockerNetworkDTO::fromJson(const QJsonObject &object, QString *error)
{
    DockerNetworkDTO dto;

    dto.id = stringValue(object, QStringLiteral("Id"));
    dto.name = stringValue(object, QStringLiteral("Name"));
    if (dto.id.isEmpty() || dto.name.isEmpty()) {
        if (error) {
            *error = QStringLiteral("network entry without Id or Name");
        }
        return std::nullopt;
    }

    dto.driver = stringValue(object, QStringLiteral("Driver"));
    dto.scope = stringValue(object, QStringLiteral("Scope"));
    dto.created = stringValue(object, QStringLiteral("Created"));
    dto.internal = boolValue(object, QStringLiteral("Internal"));
    dto.attachable = boolValue(object, QStringLiteral("Attachable"));
    dto.ingress = boolValue(object, QStringLiteral("Ingress"));
    dto.options = pairsFromObject(object.value(QStringLiteral("Options")));
    dto.labels = pairsFromObject(object.value(QStringLiteral("Labels")));

    const QJsonObject ipam = object.value(QStringLiteral("IPAM")).toObject();
    const QJsonArray configs = ipam.value(QStringLiteral("Config")).toArray();
    for (const QJsonValue &entry : configs) {
        if (!entry.isObject()) {
            continue;
        }
        const QJsonObject config = entry.toObject();
        dto.ipamConfigs.append({stringValue(config, QStringLiteral("Subnet")), stringValue(config, QStringLiteral("Gateway"))});
    }

    const QJsonObject containers = object.value(QStringLiteral("Containers")).toObject();
    dto.members.reserve(containers.size());
    for (auto it = containers.constBegin(); it != containers.constEnd(); ++it) {
        const QJsonObject entry = it.value().toObject();
        NetworkMember member;
        member.containerId = it.key();
        member.name = stringValue(entry, QStringLiteral("Name"));
        member.ipv4Address = addressWithoutPrefix(stringValue(entry, QStringLiteral("IPv4Address")));
        member.ipv6Address = addressWithoutPrefix(stringValue(entry, QStringLiteral("IPv6Address")));
        member.macAddress = stringValue(entry, QStringLiteral("MacAddress"));
        dto.members.append(member);
    }
    // Sort members by name: the daemon hands out a hash map with unstable order
    std::sort(dto.members.begin(), dto.members.end(), [](const NetworkMember &lhs, const NetworkMember &rhs) {
        if (lhs.name != rhs.name) {
            return lhs.name < rhs.name;
        }
        return lhs.containerId < rhs.containerId;
    });

    return dto;
}

QList<DockerNetworkDTO> DockerNetworkDTO::listFromJson(const QByteArray &payload, QString *error, int *skipped)
{
    QList<DockerNetworkDTO> result;

    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (!document.isArray()) {
        if (error) {
            *error = QStringLiteral("network list payload is not a JSON array");
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
        const auto dto = DockerNetworkDTO::fromJson(entry.toObject(), nullptr);
        if (!dto) {
            ++skippedCount;
            continue;
        }
        result.append(*dto);
    }

    if (skipped) {
        *skipped = skippedCount;
    }
    return result;
}

Network networkFromDto(const DockerNetworkDTO &dto)
{
    Network network;
    network.id = dto.id;
    network.name = dto.name;
    network.driver = dto.driver;
    network.scope = dto.scope;
    network.created = parseDockerTimestamp(dto.created);
    network.internal = dto.internal;
    network.attachable = dto.attachable;
    network.ingress = dto.ingress;
    network.ipamConfigs = dto.ipamConfigs;
    network.options = dto.options;
    network.labels = dto.labels;
    network.members = dto.members;
    return network;
}

QList<Network> networksFromDto(const QList<DockerNetworkDTO> &dtos)
{
    QList<Network> networks;
    networks.reserve(dtos.size());
    for (const DockerNetworkDTO &dto : dtos) {
        networks.append(networkFromDto(dto));
    }
    return networks;
}

} // namespace Kontainer
