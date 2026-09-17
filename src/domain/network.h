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

/*!
 * 创建网络时提交的内容（ARCH_V5_V8 §3.3）。
 *
 * 六期只创建 **bridge** 网络（用户已确认的范围）：`driver` 字段保留是为了让请求结构
 * 如实反映"最终发给 daemon 的是什么"，但界面只会填 `bridge`。
 */
struct NetworkCreateRequest {
    QString name;
    /*! 固定 `bridge`；其它驱动本轮只识别不创建（§3.3 的有意偏离）。 */
    QString driver = QStringLiteral("bridge");
    /*! 可选子网（例如 `172.20.0.0/16`）与网关；留空则由 daemon 自动分配。 */
    QString subnet;
    QString gateway;
    bool internal = false;
    bool attachable = false;
    QList<QPair<QString, QString>> labels;

    /*! 提交给 daemon 的 JSON（键名与 Docker API 一致；空字段不写）。 */
    QByteArray toJson() const;
};

/*!
 * 网络名称的校验规则（界面与控制器共用一份）。
 *
 * 返回空字符串表示通过；否则返回**稳定的错误 key**（文案在 QML 侧），
 * 与项目里其它校验一致（C++ 不拼用户可见文案）。
 */
QString validateNetworkName(const QString &name);
/*! 子网 / 网关的格式校验（空字符串视为"不填"）。 */
QString validateSubnet(const QString &subnet);
QString validateGateway(const QString &gateway, const QString &subnet);

} // namespace Kontainer

Q_DECLARE_METATYPE(Kontainer::Network)
Q_DECLARE_METATYPE(QList<Kontainer::Network>)
