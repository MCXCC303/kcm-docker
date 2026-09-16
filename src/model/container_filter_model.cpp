/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/container_filter_model.h"

namespace Kontainer
{

namespace
{
/*! State filter 的语义分组（写在这里，不在 QML 里拼装）。 */
bool matchesStateFilter(ContainerState state, const QString &filter)
{
    if (filter.isEmpty() || filter == QLatin1String("all")) {
        return true;
    }
    if (filter == QLatin1String("running")) {
        return state == ContainerState::Running;
    }
    if (filter == QLatin1String("paused")) {
        return state == ContainerState::Paused;
    }
    if (filter == QLatin1String("stopped")) {
        return state == ContainerState::Exited || state == ContainerState::Created;
    }
    if (filter == QLatin1String("restarting")) {
        return state == ContainerState::Restarting;
    }
    if (filter == QLatin1String("dead")) {
        return state == ContainerState::Dead;
    }
    return true;
}
} // namespace

ContainerFilterModel::ContainerFilterModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
    setSortCaseSensitivity(Qt::CaseInsensitive);
    setFilterCaseSensitivity(Qt::CaseInsensitive);
    updateSorting();

    // 行数变化时同步 count（供 UI 判断“过滤后为空”）
    connect(this, &QAbstractItemModel::rowsInserted, this, &ContainerFilterModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &ContainerFilterModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &ContainerFilterModel::countChanged);
    connect(this, &QAbstractItemModel::layoutChanged, this, &ContainerFilterModel::countChanged);
}

int ContainerFilterModel::count() const
{
    return rowCount();
}

void ContainerFilterModel::setSearchText(const QString &text)
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

void ContainerFilterModel::setStateFilter(const QString &filter)
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

void ContainerFilterModel::setSortKey(const QString &key)
{
    if (m_sortKey == key) {
        return;
    }
    m_sortKey = key;
    updateSorting();
    Q_EMIT sortKeyChanged();
}

void ContainerFilterModel::updateSorting()
{
    // ARCH_V2 §10：默认 Name 升序；created 用降序（最新在前）更符合运维直觉。
    // 注意：必须用 setSortRole() + sort(0, ...)，不能把 role 当列号传给 sort()。
    if (m_sortKey == QLatin1String("state")) {
        setSortRole(ContainerModel::StateKeyRole);
        sort(0, Qt::AscendingOrder);
    } else if (m_sortKey == QLatin1String("created")) {
        setSortRole(ContainerModel::CreatedRole);
        sort(0, Qt::DescendingOrder);
    } else {
        setSortRole(ContainerModel::NameRole);
        sort(0, Qt::AscendingOrder);
    }
}

bool ContainerFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QAbstractItemModel *source = sourceModel();
    if (!source) {
        return false;
    }

    const QModelIndex index = source->index(sourceRow, 0, sourceParent);
    if (!index.isValid()) {
        return false;
    }

    const QString stateKey = index.data(ContainerModel::StateKeyRole).toString();
    if (!matchesStateFilter(containerStateFromString(stateKey), m_stateFilter)) {
        return false;
    }

    if (m_searchText.isEmpty()) {
        return true;
    }

    // Search 覆盖 name / ID / image（§9.2），大小写不敏感
    const QString needle = m_searchText.trimmed();
    if (needle.isEmpty()) {
        return true;
    }
    const QStringList haystacks = {
        index.data(ContainerModel::NameRole).toString(),
        index.data(ContainerModel::IdRole).toString(),
        index.data(ContainerModel::ShortIdRole).toString(),
        index.data(ContainerModel::ImageRole).toString(),
    };
    for (const QString &haystack : haystacks) {
        if (haystack.contains(needle, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

bool ContainerFilterModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    if (m_sortKey == QLatin1String("state")) {
        return left.data(ContainerModel::StateKeyRole).toString() < right.data(ContainerModel::StateKeyRole).toString();
    }
    if (m_sortKey == QLatin1String("created")) {
        return left.data(ContainerModel::CreatedRole).toDateTime() < right.data(ContainerModel::CreatedRole).toDateTime();
    }
    return QString::localeAwareCompare(left.data(ContainerModel::NameRole).toString(), right.data(ContainerModel::NameRole).toString()) < 0;
}

} // namespace Kontainer
