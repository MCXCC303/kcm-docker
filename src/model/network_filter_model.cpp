/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/network_filter_model.h"

namespace Kontainer
{

namespace
{
/*! Origin filter: `all` / `predefined` (bridge/host/none) / `custom`. */
bool matchesOriginFilter(bool predefined, const QString &filter)
{
    if (filter == QLatin1String("predefined")) {
        return predefined;
    }
    if (filter == QLatin1String("custom")) {
        return !predefined;
    }
    return true;
}
} // namespace

NetworkFilterModel::NetworkFilterModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
    setSortCaseSensitivity(Qt::CaseInsensitive);
    setFilterCaseSensitivity(Qt::CaseInsensitive);
    updateSorting();

    connect(this, &QAbstractItemModel::rowsInserted, this, &NetworkFilterModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &NetworkFilterModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &NetworkFilterModel::countChanged);
    connect(this, &QAbstractItemModel::layoutChanged, this, &NetworkFilterModel::countChanged);
}

int NetworkFilterModel::count() const
{
    return rowCount();
}

void NetworkFilterModel::setSearchText(const QString &text)
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

void NetworkFilterModel::setOriginFilter(const QString &filter)
{
    if (m_originFilter == filter) {
        return;
    }
    m_originFilter = filter;
    beginFilterChange();
    endFilterChange();
    Q_EMIT originFilterChanged();
    Q_EMIT countChanged();
}

void NetworkFilterModel::setSortKey(const QString &key)
{
    if (m_sortKey == key) {
        return;
    }
    m_sortKey = key;
    updateSorting();
    Q_EMIT sortKeyChanged();
}

void NetworkFilterModel::updateSorting()
{
    // default Name ascending; members uses descending so the busiest network is visible at a glance
    if (m_sortKey == QLatin1String("driver")) {
        setSortRole(NetworkModel::DriverRole);
        sort(0, Qt::AscendingOrder);
    } else if (m_sortKey == QLatin1String("scope")) {
        setSortRole(NetworkModel::ScopeRole);
        sort(0, Qt::AscendingOrder);
    } else if (m_sortKey == QLatin1String("members")) {
        setSortRole(NetworkModel::MemberCountRole);
        sort(0, Qt::DescendingOrder);
    } else {
        setSortRole(NetworkModel::NameRole);
        sort(0, Qt::AscendingOrder);
    }
}

bool NetworkFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QAbstractItemModel *source = sourceModel();
    if (!source) {
        return false;
    }
    const QModelIndex index = source->index(sourceRow, 0, sourceParent);
    if (!index.isValid()) {
        return false;
    }

    if (!matchesOriginFilter(index.data(NetworkModel::PredefinedRole).toBool(), m_originFilter)) {
        return false;
    }

    const QString needle = m_searchText.trimmed();
    if (needle.isEmpty()) {
        return true;
    }
    const QStringList haystacks = {
        index.data(NetworkModel::NameRole).toString(),
        index.data(NetworkModel::IdRole).toString(),
        index.data(NetworkModel::ShortIdRole).toString(),
        index.data(NetworkModel::DriverRole).toString(),
        index.data(NetworkModel::SubnetRole).toString(),
    };
    for (const QString &haystack : haystacks) {
        if (haystack.contains(needle, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

bool NetworkFilterModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    if (m_sortKey == QLatin1String("driver")) {
        return left.data(NetworkModel::DriverRole).toString() < right.data(NetworkModel::DriverRole).toString();
    }
    if (m_sortKey == QLatin1String("scope")) {
        return left.data(NetworkModel::ScopeRole).toString() < right.data(NetworkModel::ScopeRole).toString();
    }
    if (m_sortKey == QLatin1String("members")) {
        return left.data(NetworkModel::MemberCountRole).toInt() < right.data(NetworkModel::MemberCountRole).toInt();
    }
    return QString::localeAwareCompare(left.data(NetworkModel::NameRole).toString(), right.data(NetworkModel::NameRole).toString()) < 0;
}

} // namespace Kontainer
