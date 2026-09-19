/*
    SPDX-FileCopyrightText: 2026 kontainer developers
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

/*! 容器接入的网络（inspect → NetworkSettings.Networks）。 */
/*!
 * 容器**声明**的宿主端口绑定（来自 inspect 的 `HostConfig.PortBindings`）。
 *
 * 与"实际发布的端口"（`ports`，来自 `NetworkSettings.Ports`）是两回事：
 * 实测 `alpine-82dc` 运行中、声明了绑定，但 `NetworkSettings.Ports` 是空的——
 * 端口页要如实分列这两种状态（用户拍板决定 3）。
 *
 * `HostPort` 可以是**区间**（实测 `WinBoat` 用 `"47300-47309"`），因此终点单独一个字段。
 */
struct DeclaredPortBinding {
    quint16 containerPort = 0;
    QString protocol; /*!< tcp / udp / sctp */
    QString hostIp; /*!< 空 = 所有接口 */
    quint16 hostPort = 0;
    /*! 区间终点；单端口时等于 `hostPort`。 */
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

/*! 容器挂载（inspect → Mounts）。 */
struct ContainerMount {
    QString type; /*!< bind / volume / tmpfs */
    QString name; /*!< 命名卷名；bind 与匿名卷为空 */
    QString source;
    QString destination;
    QString mode;
    bool readOnly = false;
};

/*!
 * 容器详情 domain object（ARCH_V2 §7/§25）。
 *
 * 这是按用户理解方式重新组织的运行信息，不是 `docker inspect` JSON 的平铺：
 * 只保留界面需要的字段，且保存原始可计算数据（QDateTime / 枚举 / 结构化列表）。
 *
 * 注意（§40）：env / labels / mount source 属于潜在敏感信息，只在用户显式展开时展示，
 * 不写日志、不做 debug 输出、不持久化。
 */
struct ContainerDetail {
    QString id;
    QString name;
    QString image;
    QString imageId;
    ContainerState state = ContainerState::Unknown;
    HealthState health = HealthState::Unknown;
    QString status; /*!< Docker 的 status 摘要，不是程序状态机（§11.2） */

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
    /*! `HostConfig.PortBindings` 里**声明**的宿主绑定（可能是区间；未声明时为空）。 */
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
     * 是否分配了 TTY（`Config.Tty`）。
     *
     * 日志流据此分形态：TTY 容器输出**原始字节**，非 TTY 是 8 字节帧的 stdcopy 流
     * （ARCH_V5_V8 §3.1.1）。判错会让日志里混进帧头字节。
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
