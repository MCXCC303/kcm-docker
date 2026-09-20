/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "model/host_port_usage.h"
#include "model/keyed_list_model.h"

namespace Kontainer
{

/*!
 * Host port list model (ARCH_next_ports.md §4.A, milestone M3).
 *
 * Data comes from `HostPortUsage` (actual publishing from the container list + declarations from
 * inspect); this only spreads `HostPortEntry` into roles for QML.
 *
 * Like the other lists it **syncs incrementally** via `KeyedListModel`: the ports page can hold hundreds
 * of rows and a full reset scrolls the view back to the top (hit more than once in this project).
 */
class HostPortModel : public KeyedListModel<HostPortModel, HostPortEntry>
{
    Q_OBJECT

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool empty READ empty NOTIFY countChanged)

public:
    enum Roles {
        PortTextRole = Qt::UserRole + 1,
        /*! Numeric port for sorting/filtering (a range's start). */
        HostPortRole,
        /*! Range end (equals start for a single port); the map uses it to highlight the whole span. */
        RangeEndRole,
        /*! Bound host address (empty = all interfaces). */
        HostIpRole,
        AddressTextRole,
        /*! Bound on **all interfaces** (IPv4/IPv6 wildcard, or both merged);
         * the UI writes "all interfaces" rather than a bare dash. */
        WildcardRole,
        ContainerPortRole,
        ProtocolRole,
        /*! `inUse` / `declaredNotPublished` (QML consumes only the key; text and icons live in QML). */
        StateKeyRole,
        ContainerNameRole,
        ContainerIdRole,
        ContainerImageRole,
        /*! Whether the row is actionable (the page uses it to choose which actions to show). */
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

    /*! Incremental sync by stable key; no signal when the content is unchanged. */
    void setEntries(const QList<HostPortEntry> &entries);

    /*! Stable key for a row: host port + container + container port + protocol (unique by construction). */
    static QString keyFor(const HostPortEntry &entry);

Q_SIGNALS:
    void countChanged();

private:
    QList<HostPortEntry> m_entries;
};

} // namespace Kontainer
