/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QString>

namespace Kontainer
{

/*!
 * One network a container is attached to.
 *
 * It lives in its own header to break an include cycle: both the container **list**
 * (`domain/container.h`) and container **detail** (`domain/container_detail.h`) need it, and the
 * detail type also needs the state enums declared in the list header.
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
