/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/host_port_filter_model.h"

#include "model/host_port_model.h"

namespace Kontainer
{

HostPortFilterModel::HostPortFilterModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
    setSortCaseSensitivity(Qt::CaseInsensitive);
    setFilterCaseSensitivity(Qt::CaseInsensitive);
    updateSorting();

    connect(this, &QAbstractItemModel::rowsInserted, this, &HostPortFilterModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &HostPortFilterModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &HostPortFilterModel::countChanged);
    connect(this, &QAbstractItemModel::layoutChanged, this, &HostPortFilterModel::countChanged);
}

int HostPortFilterModel::count() const
{
    return rowCount();
}

void HostPortFilterModel::setSearchText(const QString &text)
{
    if (m_searchText == text) {
        return;
    }
    m_searchText = text;
    beginFilterChange();
    endFilterChange();
    Q_EMIT searchTextChanged();
    Q_EMIT countChanged();
}

void HostPortFilterModel::setStateFilter(const QString &filter)
{
    if (m_stateFilter == filter) {
        return;
    }
    m_stateFilter = filter;
    beginFilterChange();
    endFilterChange();
    Q_EMIT stateFilterChanged();
    Q_EMIT countChanged();
}

void HostPortFilterModel::setSortKey(const QString &key)
{
    if (m_sortKey == key) {
        return;
    }
    m_sortKey = key;
    updateSorting();
    Q_EMIT sortKeyChanged();
}

void HostPortFilterModel::updateSorting()
{
    /*
     * With a single column, changing the sort key looks like no change at all to QSortFilterProxyModel
     * (same column and direction) -> `sort()` becomes a no-op and nothing reorders even though the key
     * changed (user-reported: sorting did nothing). An explicit invalidate() forces a re-sort with the
     * new lessThan.
     */
    sort(0, Qt::AscendingOrder);
    invalidate();
}

bool HostPortFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
    if (!index.isValid()) {
        return false;
    }

    if (m_stateFilter != QLatin1String("all")) {
        const QString stateKey = index.data(HostPortModel::StateKeyRole).toString();
        // "Not started" covers both "port still free" and "port already taken by someone else"
        const bool matches = m_stateFilter == QLatin1String("reserved")
            ? (stateKey == QLatin1String("reserved") || stateKey == QLatin1String("reservedTaken"))
            : stateKey == m_stateFilter;
        if (!matches) {
            return false;
        }
    }

    if (m_searchText.isEmpty()) {
        return true;
    }
    const QString needle = m_searchText;
    const auto matches = [&needle](const QString &haystack) {
        return haystack.contains(needle, Qt::CaseInsensitive);
    };
    return matches(index.data(HostPortModel::PortTextRole).toString())
        || matches(index.data(HostPortModel::ContainerNameRole).toString())
        || matches(index.data(HostPortModel::ContainerImageRole).toString())
        || matches(index.data(HostPortModel::AddressTextRole).toString())
        || matches(index.data(HostPortModel::ProtocolRole).toString());
}

bool HostPortFilterModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    if (m_sortKey == QLatin1String("container")) {
        const QString leftName = left.data(HostPortModel::ContainerNameRole).toString();
        const QString rightName = right.data(HostPortModel::ContainerNameRole).toString();
        if (leftName != rightName) {
            return leftName.compare(rightName, Qt::CaseInsensitive) < 0;
        }
    }
    const int leftPort = left.data(HostPortModel::HostPortRole).toInt();
    const int rightPort = right.data(HostPortModel::HostPortRole).toInt();
    if (leftPort != rightPort) {
        return leftPort < rightPort;
    }
    return left.data(HostPortModel::ContainerPortRole).toInt() < right.data(HostPortModel::ContainerPortRole).toInt();
}

} // namespace Kontainer
