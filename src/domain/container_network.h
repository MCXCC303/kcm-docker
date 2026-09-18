/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QString>

namespace Kontainer
{

/*!
 * 容器连接的一个网络。
 *
 * 单独成头文件是为了打破循环包含：容器**列表**（`domain/container.h`）与容器**详情**
 * （`domain/container_detail.h`）都需要它，而详情又依赖列表里的状态枚举。
 */
struct ContainerNetwork {
    QString name;
    QString id;
    QString ipAddress;
    QString ipv6Address;
    QString macAddress;
    QString gateway;

    friend bool operator==(const ContainerNetwork &lhs, const ContainerNetwork &rhs)
    {
        return lhs.name == rhs.name && lhs.id == rhs.id && lhs.ipAddress == rhs.ipAddress
            && lhs.ipv6Address == rhs.ipv6Address && lhs.macAddress == rhs.macAddress && lhs.gateway == rhs.gateway;
    }
};

} // namespace Kontainer
