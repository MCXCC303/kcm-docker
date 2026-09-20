/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "domain/container.h"
#include "domain/container_network.h"

#include <QDateTime>
#include <QList>
#include <QMetaType>
#include <QPair>
#include <QString>
#include <QStringList>

namespace Kontainer
{

/*! Networks the container is attached to (inspect → NetworkSettings.Networks). */
/*!
 * Host port bindings **declared** by the container (from inspect's `HostConfig.PortBindings`).
 *
 * Distinct from the actually published ports (`ports`, from `NetworkSettings.Ports`): the running
 * `alpine-82dc` had declared bindings while `NetworkSettings.Ports` stayed empty, so the ports page
 * lists the two states side by side (user decision 3).
 *
 * `HostPort` may be a **range** (`WinBoat` used `"47300-47309"`), hence the separate end field.
 */
struct DeclaredPortBinding {
    quint16 containerPort = 0;
    QString protocol; /*!< tcp / udp / sctp */
    QString hostIp; /*!< empty = all interfaces */
    quint16 hostPort = 0;
    /*! Range end; equals `hostPort` for a single port. */
    quint16 hostPortEnd = 0;

    bool isRange() const
    {
        return hostPortEnd != 0 && hostPortEnd != hostPort;
    }
    friend bool operator==(const DeclaredPortBinding &lhs, const DeclaredPortBinding &rhs)
    {
        return lhs.containerPort == rhs.containerPort && lhs.protocol == rhs.protocol && lhs.hostIp == rhs.hostIp
            && lhs.hostPort == rhs.hostPort && lhs.hostPortEnd == rhs.hostPortEnd;
    }
};

/*! Container mount (inspect → Mounts). */
struct ContainerMount {
    QString type; /*!< bind / volume / tmpfs */
    QString name; /*!< named volume name; empty for bind and anonymous volumes */
    QString source;
    QString destination;
    QString mode;
    bool readOnly = false;
};

/*!
 * Container detail domain object (ARCH_V2 §7/§25).
 *
 * Runtime information reorganized the way users understand it, not a flattening of the
 * `docker inspect` JSON: only fields the UI needs, kept as raw computable data (QDateTime / enums /
 * structured lists).
 *
 * Note (§40): env / labels / mount source are potentially sensitive — shown only when the user
 * explicitly expands them, never logged, debug-printed or persisted.
 */
struct ContainerDetail {
    QString id;
    QString name;
    QString image;
    QString imageId;
    ContainerState state = ContainerState::Unknown;
    HealthState health = HealthState::Unknown;
    QString status; /*!< Docker's status summary, not the program state machine (§11.2) */

    QDateTime created;
    QDateTime started;
    QDateTime finished;
    int exitCode = 0;
    bool oomKilled = false;
    bool restarting = false;
    bool paused = false;
    bool dead = false;
    int restartCount = 0;
    int pid = 0;
    QString platform;

    QList<Port> ports;
    /*! Host bindings **declared** in `HostConfig.PortBindings` (may be ranges; empty when none). */
    QList<DeclaredPortBinding> declaredPorts;
    QList<ContainerNetwork> networks;
    QList<ContainerMount> mounts;

    QStringList environment;
    QStringList command;
    QStringList entrypoint;
    QString workingDirectory;
    QString user;
    QString hostname;
    /*!
     * Whether a TTY is allocated (`Config.Tty`).
     *
     * This selects the log stream format: a TTY container emits **raw bytes**, a non-TTY one emits
     * the 8-byte-framed stdcopy stream (ARCH_V5_V8 §3.1.1). Getting it wrong mixes frame-header
     * bytes into the log.
     */
    bool tty = false;
    QList<QPair<QString, QString>> labels;
    QString restartPolicy;

    bool isValid() const
    {
        return !id.isEmpty();
    }
};

} // namespace Kontainer

Q_DECLARE_METATYPE(Kontainer::ContainerDetail)
