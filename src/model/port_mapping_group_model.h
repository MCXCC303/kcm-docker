/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
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
 * One container port with all of its host bindings (ARCH_V5_V8 §2.1 topology revision).
 *
 * Why group multiple mappings: one container port mapping to several host addresses is normal
 * (`0.0.0.0:8888` + `:::8888` + `127.0.0.1:18888`), and one row per mapping repeats the same port
 * all down the left column. Grouped, the left column holds one chip and **branches** connect it to
 * every binding.
 */
struct PortMappingGroup {
    quint16 containerPort = 0;
    /*! tcp / udp / sctp. */
    QString protocol;
    /*! All **published** bindings of this port (sorted by host port, host address, for a stable order). */
    QList<PortMappingEntry> bindings;

    /*! Container-side chip text, e.g. `80/tcp`. */
    QString containerChipText() const;
    /*! Text of each chip on the right (same order as `bindings`). */
    QStringList hostChipTexts() const;

    friend bool operator==(const PortMappingGroup &lhs, const PortMappingGroup &rhs)
    {
        return lhs.containerPort == rhs.containerPort && lhs.protocol == rhs.protocol && lhs.bindings == rhs.bindings;
    }
};

/*!
 * Grouped port mapping model (for the topology view).
 *
 * Grouping rule: published mappings with the same **(container port, protocol)** form one group.
 * Unpublished ports stay out (they have no host endpoint and would only leave dangling lines in the
 * topology); `PortMappingModel` still presents them in their own group.
 *
 * Order: groups by container port then protocol ascending; bindings inside a group by host port then
 * host address ascending — deterministic, so rows never jump on refresh.
 */
class PortMappingGroupModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool empty READ empty NOTIFY countChanged)
    /*!
     * Total number of bindings (sum over all groups).
     *
     * The topology view **computes** its own height from it (`header + rows * rowHeight`): reading a
     * measured value adds a frame of "measure → layout → draw lines", which briefly misplaces the
     * lines on refresh (ARCH_V4 §2.1.2).
     */
    Q_PROPERTY(int bindingCount READ bindingCount NOTIFY countChanged)

public:
    enum Roles {
        ContainerPortRole = Qt::UserRole + 1,
        ProtocolRole,
        ContainerChipTextRole,
        /*! Chip text per binding on the right (QStringList); QML lays out the right column from it. */
        HostChipTextsRole,
        /*!
         * Boolean list in the same order as `hostChipTexts`: whether the binding stands for both
         * IPv4 + IPv6 (`dualStack`).
         *
         * The topology view draws the host endpoint ring as a double ring (two colours) from it.
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
    /*! Group by (container port, protocol); unchanged content emits nothing. */
    void setEntries(const QList<PortMappingEntry> &entries);

Q_SIGNALS:
    void countChanged();

private:
    QList<PortMappingGroup> m_groups;
};

} // namespace Kontainer
