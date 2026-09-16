/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/image_filter_model.h"

#include <QStringList>

namespace Kontainer
{

ImageFilterModel::ImageFilterModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
    setSortCaseSensitivity(Qt::CaseInsensitive);
    setFilterCaseSensitivity(Qt::CaseInsensitive);
    updateSorting();

    connect(this, &QAbstractItemModel::rowsInserted, this, &ImageFilterModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &ImageFilterModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &ImageFilterModel::countChanged);
    connect(this, &QAbstractItemModel::layoutChanged, this, &ImageFilterModel::countChanged);
}

int ImageFilterModel::count() const
{
    return rowCount();
}

void ImageFilterModel::setSearchText(const QString &text)
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

void ImageFilterModel::setUseFilter(const QString &filter)
{
    if (m_useFilter == filter) {
        return;
    }
    m_useFilter = filter;
    beginFilterChange();
    endFilterChange();
    Q_EMIT useFilterChanged();
    Q_EMIT countChanged();
}

void ImageFilterModel::setSortKey(const QString &key)
{
    if (m_sortKey == key) {
        return;
    }
    m_sortKey = key;
    updateSorting();
    Q_EMIT sortKeyChanged();
}

void ImageFilterModel::updateSorting()
{
    // ARCH_V2 §10：默认 Repository 升序（同样必须用 setSortRole）
    if (m_sortKey == QLatin1String("created")) {
        setSortRole(ImageModel::CreatedRole);
        sort(0, Qt::DescendingOrder);
    } else if (m_sortKey == QLatin1String("size")) {
        setSortRole(ImageModel::SizeBytesRole);
        sort(0, Qt::DescendingOrder);
    } else {
        setSortRole(ImageModel::PrimaryTagRole);
        sort(0, Qt::AscendingOrder);
    }
}

bool ImageFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QAbstractItemModel *source = sourceModel();
    if (!source) {
        return false;
    }
    const QModelIndex index = source->index(sourceRow, 0, sourceParent);
    if (!index.isValid()) {
        return false;
    }

    if (m_useFilter == QLatin1String("in-use") && !index.data(ImageModel::InUseRole).toBool()) {
        return false;
    }
    if (m_useFilter == QLatin1String("dangling") && !index.data(ImageModel::DanglingRole).toBool()) {
        return false;
    }

    const QString needle = m_searchText.trimmed();
    if (needle.isEmpty()) {
        return true;
    }

    if (index.data(ImageModel::IdRole).toString().contains(needle, Qt::CaseInsensitive)) {
        return true;
    }
    if (index.data(ImageModel::ShortIdRole).toString().contains(needle, Qt::CaseInsensitive)) {
        return true;
    }
    const QStringList tags = index.data(ImageModel::TagsRole).toStringList();
    for (const QString &tag : tags) {
        if (tag.contains(needle, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

bool ImageFilterModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    if (m_sortKey == QLatin1String("created")) {
        return left.data(ImageModel::CreatedRole).toDateTime() < right.data(ImageModel::CreatedRole).toDateTime();
    }
    if (m_sortKey == QLatin1String("size")) {
        return left.data(ImageModel::SizeBytesRole).toLongLong() < right.data(ImageModel::SizeBytesRole).toLongLong();
    }
    return QString::localeAwareCompare(left.data(ImageModel::PrimaryTagRole).toString(), right.data(ImageModel::PrimaryTagRole).toString()) < 0;
}

} // namespace Kontainer
