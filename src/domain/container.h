/*
    SPDX-FileCopyrightText: 2026 kontainer developers
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

/*! 容器状态语义（ARCH_V1 §2.1：created/restarting/running/removing/paused/exited/dead）。 */
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

/*! 健康状态；Unknown 表示引擎/API 没有提供该信息（与 None「未配置健康检查」不同）。 */
enum class HealthState {
    Unknown,
    None,
    Starting,
    Healthy,
    Unhealthy,
};

/*! 端口映射（domain model，不直接暴露 Docker JSON 结构）。 */
struct Port {
    QString ip;
    quint16 privatePort = 0;
    quint16 publicPort = 0; /*!< 0 表示未发布到宿主 */
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
 * 容器 domain object（ARCH_V1 §12.1）。
 *
 * 保存原始可计算数据（QDateTime、枚举、结构化端口），不保存任何 UI 字符串，
 * 也不保留 Docker API 的 JSON 结构。
 */
class Container
{
public:
    QString id;
    QString name;
    QString image;
    QString imageId;
    QString status; /*!< Docker 返回的 status 文本（数据，非界面文案） */
    ContainerState state = ContainerState::Unknown;
    HealthState health = HealthState::Unknown;
    QDateTime created; /*!< UTC */
    QList<Port> ports;
    /*!
     * 该容器连接的网络（来自 `GET /containers/json` 的 `NetworkSettings.Networks`）。
     *
     * **网络成员列表的唯一可靠来源**：实测 `GET /networks` 返回的 `Containers` 是空的
     * （只有 `GET /networks/{id}` 才填），因此"哪些容器连了这个网络"必须从容器侧汇总。
     */
    QList<ContainerNetwork> networks;

    bool isValid() const
    {
        return !id.isEmpty();
    }
    /*! 12 位短 ID（Docker 习惯用法）。 */
    QString shortId() const;
    /*! 稳定的状态键，供 QML 做展示判断："running" / "exited" / ... */
    QString stateKey() const;
    /*! 稳定的健康键："healthy" / "unhealthy" / "starting" / "none" / "unknown" */
    QString healthKey() const;

    /*!
     * 值比较：后台刷新时如果数据完全没变，模型就不需要发任何信号，
     * 从而避免每 5 秒重置一次列表（滚动位置与 delegate 都保持不动，ARCH_V2 §32/§34）。
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
