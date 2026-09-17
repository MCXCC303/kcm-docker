/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QDateTime>
#include <QList>
#include <QMetaType>
#include <QPair>
#include <QString>
#include <QStringList>

namespace Kontainer
{

/*!
 * 网络里连接的一个容器（`GET /networks` 的 `Containers` 映射）。
 *
 * 这是**只读快照**：容器与网络的连接关系以 daemon 为准，界面不用它做任何推断。
 */
struct NetworkMember {
    QString containerId;
    QString name;
    /*! 去掉子网前缀后的地址（daemon 给的是 `172.18.0.2/16`）。 */
    QString ipv4Address;
    QString ipv6Address;
    QString macAddress;

    bool operator==(const NetworkMember &other) const
    {
        return containerId == other.containerId && name == other.name && ipv4Address == other.ipv4Address
            && ipv6Address == other.ipv6Address && macAddress == other.macAddress;
    }
};

/*!
 * Docker 网络（ARCH_V5_V8 §3.2）。
 *
 * 六期只**创建 bridge 网络**（§3.3），但列表与详情要如实展示其它驱动
 * （overlay / macvlan / host / null…）：识别与展示不需要额外权限，创建才需要。
 */
struct Network {
    QString id;
    QString name;
    /*! bridge / host / null / overlay / macvlan …（`null` 就是 none 网络）。 */
    QString driver;
    /*! local / swarm / global。 */
    QString scope;
    QDateTime created;
    bool internal = false;
    bool attachable = false;
    bool ingress = false;
    /*! IPAM 配置（子网 + 网关）；host/none 网络通常为空。 */
    QList<QPair<QString, QString>> ipamConfigs;
    /*! 驱动选项（键值对，按 key 排序）。 */
    QList<QPair<QString, QString>> options;
    QList<QPair<QString, QString>> labels;
    QList<NetworkMember> members;

    bool isValid() const
    {
        return !id.isEmpty() && !name.isEmpty();
    }
    /*! 12 位短 ID。 */
    QString shortId() const;
    /*!
     * 是否是 daemon 预定义的网络（`bridge` / `host` / `none`）。
     *
     * 判定用名字：daemon 自己也是这么做的——删除时会回
     * `403 bridge is a pre-defined network and cannot be removed`（附录 A.3）。
     * 界面据此**不显示删除入口**；万一还有漏网的（例如更老的引擎），
     * 403 也会被映射成"用户可自行解决"的文案（§3.2）。
     */
    bool isPredefined() const;
    /*! 全部子网，用 `, ` 连接（没有则空）。 */
    QString subnetText() const;
    /*! 主 IPAM 网关（没有则空）。 */
    QString primaryGateway() const;
    /*! 连接到此网络的容器数。 */
    int memberCount() const;
    /*! 第一个成员之外还有多少容器（列表里显示 `+N`）。 */
    int extraMemberCount() const;

    bool operator==(const Network &other) const;
};

} // namespace Kontainer

Q_DECLARE_METATYPE(Kontainer::Network)
Q_DECLARE_METATYPE(QList<Kontainer::Network>)
