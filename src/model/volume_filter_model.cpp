/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/volume_filter_model.h"

namespace Kontainer
{

namespace
{
/*! Usage filter: `all` / `unused` / `inUse` (unknown usage falls into neither side). */
bool matchesUsageFilter(bool inUse, bool usageKnown, const QString &filter)
{
    if (filter == QLatin1String("unused")) {
        return usageKnown && !inUse;
    }
    if (filter == QLatin1String("inUse")) {
        return usageKnown && inUse;
    }
    return true;
}
} // namespace

VolumeFilterModel::VolumeFilterModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
    setSortCaseSensitivity(Qt::CaseInsensitive);
    setFilterCaseSensitivity(Qt::CaseInsensitive);
    updateSorting();

    connect(this, &QAbstractItemModel::rowsInserted, this, &VolumeFilterModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &VolumeFilterModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &VolumeFilterModel::countChanged);
    connect(this, &QAbstractItemModel::layoutChanged, this, &VolumeFilterModel::countChanged);
}

int VolumeFilterModel::count() const
{
    return rowCount();
}

void VolumeFilterModel::setSearchText(const QString &text)
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

void VolumeFilterModel::setUsageFilter(const QString &filter)
{
    if (m_usageFilter == filter) {
        return;
    }
    m_usageFilter = filter;
    beginFilterChange();
    endFilterChange();
    Q_EMIT usageFilterChanged();
    Q_EMIT countChanged();
}

void VolumeFilterModel::setSortKey(const QString &key)
{
    if (m_sortKey == key) {
        return;
    }
    m_sortKey = key;
    updateSorting();
    Q_EMIT sortKeyChanged();
}

void VolumeFilterModel::updateSorting()
{
    if (m_sortKey == QLatin1String("driver")) {
        setSortRole(VolumeModel::DriverRole);
        sort(0, Qt::AscendingOrder);
    } else if (m_sortKey == QLatin1String("size")) {
        setSortRole(VolumeModel::SizeBytesRole);
        sort(0, Qt::DescendingOrder); // biggest first (most useful when cleaning up)
    } else if (m_sortKey == QLatin1String("refs")) {
        setSortRole(VolumeModel::RefCountRole);
        sort(0, Qt::DescendingOrder);
    } else {
        setSortRole(VolumeModel::NameRole);
        sort(0, Qt::AscendingOrder);
    }
}

bool VolumeFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QAbstractItemModel *source = sourceModel();
    if (!source) {
        return false;
    }
    const QModelIndex index = source->index(sourceRow, 0, sourceParent);
    if (!index.isValid()) {
        return false;
    }

    if (!matchesUsageFilter(index.data(VolumeModel::InUseRole).toBool(),
                            index.data(VolumeModel::UsageKnownRole).toBool(),
                            m_usageFilter)) {
        return false;
    }

    const QString needle = m_searchText.trimmed();
    if (needle.isEmpty()) {
        return true;
    }
    const QStringList haystacks = {
        index.data(VolumeModel::NameRole).toString(),
        index.data(VolumeModel::DriverRole).toString(),
        index.data(VolumeModel::MountpointRole).toString(),
    };
    for (const QString &haystack : haystacks) {
        if (haystack.contains(needle, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

bool VolumeFilterModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    if (m_sortKey == QLatin1String("driver")) {
        return left.data(VolumeModel::DriverRole).toString() < right.data(VolumeModel::DriverRole).toString();
    }
    if (m_sortKey == QLatin1String("size")) {
        return left.data(VolumeModel::SizeBytesRole).toLongLong() < right.data(VolumeModel::SizeBytesRole).toLongLong();
    }
    if (m_sortKey == QLatin1String("refs")) {
        return left.data(VolumeModel::RefCountRole).toInt() < right.data(VolumeModel::RefCountRole).toInt();
    }
    return QString::localeAwareCompare(left.data(VolumeModel::NameRole).toString(), right.data(VolumeModel::NameRole).toString()) < 0;
}

} // namespace Kontainer
