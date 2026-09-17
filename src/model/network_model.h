/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "domain/network.h"

#include <QAbstractListModel>
#include <QList>

namespace Kontainer
{

/*!
 * 网络列表模型（ARCH_V5_V8 §3.2）。
 *
 * 与容器/镜像列表一样：内容未变不发信号（后台刷新不重建 delegate）；
 * 排序交给代理模型，模型本身只按 daemon 给的顺序保存。
 *
 * 列表页需要的信息（名称、驱动、范围、子网、成员数、是否内置）都在 role 里，
 * 详情页需要的成员/标签/选项也在——`/networks` 返回的就是完整对象（§3.2 实测），
 * 因此详情页直接用同一份数据，不再单独发请求。
 */
class NetworkModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool empty READ empty NOTIFY countChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        IdRole,
        ShortIdRole,
        DriverRole,
        ScopeRole,
        CreatedRole,
        SubnetRole,
        GatewayRole,
        /*! 是否是 daemon 预定义网络（bridge/host/none）——界面据此隐藏删除入口。 */
        PredefinedRole,
        InternalRole,
        AttachableRole,
        IngressRole,
        MemberCountRole,
        /*! 成员容器（`QList<NetworkMember>`），详情页用。 */
        MembersRole,
        /*! 标签与驱动选项（`QList<QPair<QString,QString>>`）。 */
        LabelsRole,
        OptionsRole,
    };
    Q_ENUM(Roles)

    explicit NetworkModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    bool empty() const;
    const QList<Network> &networks() const;
    /*! 内容未变则完全不动模型。 */
    void setNetworks(const QList<Network> &networks);
    /*! 清空（失败时不让旧数据继续冒充最新）。 */
    void clear();

    /*! 按 Id 找行；找不到返回 -1（详情页导航用）。 */
    Q_INVOKABLE int rowForId(const QString &id) const;

Q_SIGNALS:
    void countChanged();

private:
    QList<Network> m_networks;
};

} // namespace Kontainer
