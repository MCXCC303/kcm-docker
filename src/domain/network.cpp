/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "domain/network.h"

namespace Kontainer
{

QString Network::shortId() const
{
    return id.left(12);
}

bool Network::isPredefined() const
{
    // daemon 的判定就是名字（实测错误文案：`bridge is a pre-defined network and cannot be removed`）
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

bool Network::operator==(const Network &other) const
{
    return id == other.id && name == other.name && driver == other.driver && scope == other.scope && created == other.created
        && internal == other.internal && attachable == other.attachable && ingress == other.ingress
        && ipamConfigs == other.ipamConfigs && options == other.options && labels == other.labels && members == other.members;
}

} // namespace Kontainer
