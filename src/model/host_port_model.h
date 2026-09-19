/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "model/host_port_usage.h"
#include "model/keyed_list_model.h"

namespace Kontainer
{

/*!
 * 宿主端口列表模型（ARCH_next_ports.md §4.A，里程碑 M3）。
 *
 * 数据来自 `HostPortUsage`（容器列表的"实际发布" + inspect 的"声明"），
 * 只负责把 `HostPortEntry` 摊成 role 给 QML。
 *
 * 与其它列表一样用 `KeyedListModel` **增量同步**：端口页可能有上百行，
 * 整表重置会把滚动位置拉回顶部（这个坑本项目踩过不止一次）。
 */
class HostPortModel : public KeyedListModel<HostPortModel, HostPortEntry>
{
    Q_OBJECT

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool empty READ empty NOTIFY countChanged)

public:
    enum Roles {
        PortTextRole = Qt::UserRole + 1,
        /*! 排序/过滤用的数字端口（区间时是起点）。 */
        HostPortRole,
        /*! 区间终点（单端口时等于起点）；地图要靠它把整段点亮。 */
        RangeEndRole,
        /*! 绑定的宿主地址（空 = 所有接口）。 */
        HostIpRole,
        AddressTextRole,
        /*! 绑定在**所有接口**上（IPv4/IPv6 通配，或两者合并）——界面上写"所有接口"而不是一个破折号。 */
        WildcardRole,
        ContainerPortRole,
        ProtocolRole,
        /*! `inUse` / `declaredNotPublished`（QML 只认 key，文案与图标在 QML）。 */
        StateKeyRole,
        ContainerNameRole,
        ContainerIdRole,
        ContainerImageRole,
        /*! 一行是否可用（页面据此决定显示哪些动作）。 */
        ActionableRole,
    };
    Q_ENUM(Roles)

    explicit HostPortModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    bool empty() const;
    const QList<HostPortEntry> &entries() const;

    /*! 按稳定键增量同步；内容未变不发信号。 */
    void setEntries(const QList<HostPortEntry> &entries);

    /*! 一行的稳定键：宿主端口 + 容器 + 容器端口 + 协议（同一端口可以有多个容器？不会，但保持唯一）。 */
    static QString keyFor(const HostPortEntry &entry);

Q_SIGNALS:
    void countChanged();

private:
    QList<HostPortEntry> m_entries;
};

} // namespace Kontainer
