/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "domain/container_network.h"

#include <QDateTime>
#include <QList>
#include <QMetaType>
#include <QString>

namespace Kontainer
{

/*! Container state semantics (ARCH_V1 §2.1: created/restarting/running/removing/paused/exited/dead). */
enum class ContainerState {
    Unknown,
    Created,
    Restarting,
    Running,
    Removing,
    Paused,
    Exited,
    Dead,
};

/*! Health state; Unknown means the engine/API gave nothing (unlike None, "no health check configured"). */
enum class HealthState {
    Unknown,
    None,
    Starting,
    Healthy,
    Unhealthy,
};

/*! Port mapping (domain model; does not expose the Docker JSON structure). */
struct Port {
    QString ip;
    quint16 privatePort = 0;
    quint16 publicPort = 0; /*!< 0 means not published to the host */
    QString type;

    bool isPublished() const
    {
        return publicPort != 0;
    }

    friend bool operator==(const Port &lhs, const Port &rhs)
    {
        return lhs.ip == rhs.ip && lhs.privatePort == rhs.privatePort && lhs.publicPort == rhs.publicPort && lhs.type == rhs.type;
    }
};

/*!
 * Container domain object (ARCH_V1 §12.1).
 *
 * Holds raw computable data (QDateTime, enums, structured ports); no UI strings and no Docker API
 * JSON structure.
 */
class Container
{
public:
    QString id;
    QString name;
    QString image;
    QString imageId;
    QString status; /*!< status text returned by Docker (data, not UI copy) */
    ContainerState state = ContainerState::Unknown;
    HealthState health = HealthState::Unknown;
    QDateTime created; /*!< UTC */
    QList<Port> ports;
    /*!
     * Networks this container is attached to (from `NetworkSettings.Networks` of the list call).
     *
     * **The only reliable source of network membership**: `Containers` in the `GET /networks`
     * response came back empty (only `GET /networks/{id}` fills it), so "which containers are on
     * this network" has to be aggregated from the container side.
     */
    QList<ContainerNetwork> networks;

    bool isValid() const
    {
        return !id.isEmpty();
    }
    /*! 12-character short ID (the customary Docker form). */
    QString shortId() const;
    /*! Stable state key for QML display decisions: "running" / "exited" / ... */
    QString stateKey() const;
    /*! Stable health key: "healthy" / "unhealthy" / "starting" / "none" / "unknown" */
    QString healthKey() const;

    /*!
     * Value comparison: when a background refresh changed nothing, the model need not emit any
     * signal, so the list is not reset every 5 seconds (scroll position and delegates stay put,
     * ARCH_V2 §32/§34).
     */
    friend bool operator==(const Container &lhs, const Container &rhs)
    {
        return lhs.id == rhs.id && lhs.name == rhs.name && lhs.image == rhs.image && lhs.imageId == rhs.imageId && lhs.status == rhs.status
            && lhs.state == rhs.state && lhs.health == rhs.health && lhs.created == rhs.created && lhs.ports == rhs.ports;
    }
};

ContainerState containerStateFromString(const QString &state);
QString containerStateKey(ContainerState state);
HealthState healthStateFromString(const QString &health);
QString healthStateKey(HealthState health);

} // namespace Kontainer

Q_DECLARE_METATYPE(Kontainer::Container)
