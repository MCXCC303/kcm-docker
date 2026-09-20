/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "domain/network.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace Kontainer
{

QString Network::shortId() const
{
    return id.left(12);
}

bool Network::isPredefined() const
{
    // The daemon also decides by name (observed 403: `bridge is a pre-defined network ...`)
    return name == QLatin1String("bridge") || name == QLatin1String("host") || name == QLatin1String("none");
}

QString Network::subnetText() const
{
    QStringList subnets;
    for (const auto &config : ipamConfigs) {
        if (!config.first.isEmpty()) {
            subnets.append(config.first);
        }
    }
    return subnets.join(QStringLiteral(", "));
}

QString Network::primaryGateway() const
{
    for (const auto &config : ipamConfigs) {
        if (!config.second.isEmpty()) {
            return config.second;
        }
    }
    return {};
}

int Network::memberCount() const
{
    return int(members.size());
}

int Network::extraMemberCount() const
{
    return members.size() > 1 ? int(members.size()) - 1 : 0;
}

QByteArray NetworkCreateRequest::toJson() const
{
    QJsonObject root;
    root.insert(QStringLiteral("Name"), name);
    root.insert(QStringLiteral("Driver"), driver.isEmpty() ? QStringLiteral("bridge") : driver);
    root.insert(QStringLiteral("Internal"), internal);
    root.insert(QStringLiteral("Attachable"), attachable);

    if (!labels.isEmpty()) {
        QJsonObject labelObject;
        for (const auto &label : labels) {
            if (!label.first.isEmpty()) {
                labelObject.insert(label.first, label.second);
            }
        }
        root.insert(QStringLiteral("Labels"), labelObject);
    }

    if (!subnet.isEmpty() || !gateway.isEmpty()) {
        QJsonObject config;
        if (!subnet.isEmpty()) {
            config.insert(QStringLiteral("Subnet"), subnet);
        }
        if (!gateway.isEmpty()) {
            config.insert(QStringLiteral("Gateway"), gateway);
        }
        QJsonArray configs;
        configs.append(config);
        QJsonObject ipam;
        ipam.insert(QStringLiteral("Driver"), QStringLiteral("default"));
        ipam.insert(QStringLiteral("Config"), configs);
        root.insert(QStringLiteral("IPAM"), ipam);
    }

    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QString validateNetworkName(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return QStringLiteral("nameRequired");
    }
    // Docker network names allow alphanumerics, _ . - but no whitespace (the daemon rejects spaces)
    static const QRegularExpression allowed(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.-]*$"));
    if (!allowed.match(trimmed).hasMatch()) {
        return QStringLiteral("nameInvalid");
    }
    return {};
}

QString validateSubnet(const QString &subnet)
{
    const QString trimmed = subnet.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }
    // Only check that it looks like CIDR: the daemon decides real validity (it also checks overlaps)
    static const QRegularExpression cidr(QStringLiteral("^[0-9a-fA-F:.]+/[0-9]{1,3}$"));
    if (!cidr.match(trimmed).hasMatch()) {
        return QStringLiteral("subnetInvalid");
    }
    return {};
}

QString validateGateway(const QString &gateway, const QString &subnet)
{
    const QString trimmed = gateway.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }
    // The gateway must lie inside the subnet, otherwise the daemon reports a hard-to-read error
    if (subnet.trimmed().isEmpty()) {
        return QStringLiteral("gatewayNeedsSubnet");
    }
    static const QRegularExpression address(QStringLiteral("^[0-9a-fA-F:.]+$"));
    if (!address.match(trimmed).hasMatch()) {
        return QStringLiteral("gatewayInvalid");
    }
    return {};
}

bool Network::operator==(const Network &other) const
{
    return id == other.id && name == other.name && driver == other.driver && scope == other.scope && created == other.created
        && internal == other.internal && attachable == other.attachable && ingress == other.ingress
        && ipamConfigs == other.ipamConfigs && options == other.options && labels == other.labels && members == other.members;
}

} // namespace Kontainer
