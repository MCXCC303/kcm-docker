/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

namespace Kontainer
{

/*!
 * `NetworkSettings.Networks.<name>`：某个容器接在哪个网络上。
 *
 * 注意与 `dto/network_dto.h` 的 `DockerNetworkDTO`（`GET /networks` 的**网络对象**）区分：
 * 字段完全不同。单独成头文件是因为容器**列表**与容器 **inspect** 都要用它，
 * 而那两个 DTO 头文件互相包含。
 */
struct ContainerNetworkDTO {
    QString name;
    QString networkId;
    QString ipAddress;
    QString ipv6Address;
    QString macAddress;
    QString gateway;
};

/*! 解析 `NetworkSettings.Networks`（列表与 inspect 的载荷形状一致，复用同一实现）。 */
QList<ContainerNetworkDTO> parseNetworks(const QJsonObject &networkSettings);

} // namespace Kontainer
