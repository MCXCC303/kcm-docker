/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "model/port_mapping_model.h"

#include <QAbstractListModel>
#include <QList>
#include <QStringList>

namespace Kontainer
{

/*!
 * 一个容器端口及其全部宿主绑定（ARCH_V5_V8 §2.1 拓扑形态修订）。
 *
 * 为什么要把"多条映射"合成一组：同一个容器端口映射到多个宿主地址是常态
 * （`0.0.0.0:8888` + `:::8888` + `127.0.0.1:18888`），一行一条会把同一个端口
 * 在左列重复写很多遍。合成一组后左列只有一枚芯片，右边用**分支**连到每个绑定。
 */
struct PortMappingGroup {
    quint16 containerPort = 0;
    /*! tcp / udp / sctp。 */
    QString protocol;
    /*! 该容器端口的全部**已发布**绑定（按宿主端口、宿主地址排序，保证顺序稳定）。 */
    QList<PortMappingEntry> bindings;

    /*! 容器侧芯片文本，例如 `80/tcp`。 */
    QString containerChipText() const;
    /*! 右侧每一枚芯片的文本（顺序与 `bindings` 一致）。 */
    QStringList hostChipTexts() const;

    friend bool operator==(const PortMappingGroup &lhs, const PortMappingGroup &rhs)
    {
        return lhs.containerPort == rhs.containerPort && lhs.protocol == rhs.protocol && lhs.bindings == rhs.bindings;
    }
};

/*!
 * 分组后的端口映射模型（给拓扑图用）。
 *
 * 分组规则：**(容器端口, 协议)** 相同的已发布映射合成一组。未发布的端口不进这里
 * （它们没有宿主端点，硬塞进拓扑只会留下悬空的线），仍由 `PortMappingModel`
 * 单独成组呈现。
 *
 * 排序：组按容器端口、协议升序；组内绑定按宿主端口、宿主地址升序——
 * 顺序确定，刷新时行不会跳。
 */
class PortMappingGroupModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool empty READ empty NOTIFY countChanged)
    /*!
     * 全部绑定的总数（各组绑定数之和）。
     *
     * 拓扑图据此**算出**自己的高度（`header + 行数 * rowHeight`）：读测量值会引入
     * "先测量 → 再布局 → 再画线"的一帧延迟，刷新时连线会短暂错位（ARCH_V4 §2.1.2）。
     */
    Q_PROPERTY(int bindingCount READ bindingCount NOTIFY countChanged)

public:
    enum Roles {
        ContainerPortRole = Qt::UserRole + 1,
        ProtocolRole,
        ContainerChipTextRole,
        /*! 右侧各绑定的芯片文本（QStringList）——QML 用它铺右列。 */
        HostChipTextsRole,
        /*!
         * 与 `hostChipTexts` 同序的布尔表：该绑定是否同时代表 IPv4 + IPv6（`dualStack`）。
         *
         * 拓扑图据此把宿主端点的圆环画成双环（内外两种颜色）。
         */
        DualStackFlagsRole,
        BindingCountRole,
    };
    Q_ENUM(Roles)

    explicit PortMappingGroupModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    bool empty() const;
    int bindingCount() const;
    const QList<PortMappingGroup> &groups() const;
    /*! 按 (容器端口, 协议) 分组；内容未变不发信号。 */
    void setEntries(const QList<PortMappingEntry> &entries);

Q_SIGNALS:
    void countChanged();

private:
    QList<PortMappingGroup> m_groups;
};

} // namespace Kontainer
