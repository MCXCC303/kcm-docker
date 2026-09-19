/*
    SPDX-FileCopyrightText: 2026 kontainer developers
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
    if (m_sortKey == QLatin1String("container")) {
        sort(0, Qt::AscendingOrder);
    } else {
        sort(0, Qt::AscendingOrder); // 端口升序：proxy 的 lessThan 决定比较哪一列
    }
}

bool HostPortFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
    if (!index.isValid()) {
        return false;
    }

    if (m_stateFilter != QLatin1String("all")) {
        const QString stateKey = index.data(HostPortModel::StateKeyRole).toString();
        if (stateKey != m_stateFilter) {
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
