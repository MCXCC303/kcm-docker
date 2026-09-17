/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "model/volume_model.h"

#include <QSortFilterProxyModel>
#include <QString>

namespace Kontainer
{

/*!
 * 数据卷列表的搜索 / 过滤 / 排序代理（ARCH_V5_V8 §3.5）。
 *
 * - Search：名称 / 驱动 / 挂载点
 * - Filter：全部 / 未使用（`unused`；prune 的目标就是它们）/ 使用中（`inUse`）
 *   —— 注意"引用数未知"既不算未使用也不算使用中，它只在"全部"里出现，
 *   界面也会明说"未知"（把未知当成 0 会让用户误以为可以安全清理）
 * - 排序：名称（默认）/ 驱动 / 大小（降序）/ 引用数（降序）
 */
class VolumeFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT

    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
    /*! "all" / "unused" / "inUse" */
    Q_PROPERTY(QString usageFilter READ usageFilter WRITE setUsageFilter NOTIFY usageFilterChanged)
    /*! "name" / "driver" / "size" / "refs" */
    Q_PROPERTY(QString sortKey READ sortKey WRITE setSortKey NOTIFY sortKeyChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    explicit VolumeFilterModel(QObject *parent = nullptr);

    QString searchText() const
    {
        return m_searchText;
    }
    void setSearchText(const QString &text);

    QString usageFilter() const
    {
        return m_usageFilter;
    }
    void setUsageFilter(const QString &filter);

    QString sortKey() const
    {
        return m_sortKey;
    }
    void setSortKey(const QString &key);

    int count() const;

Q_SIGNALS:
    void searchTextChanged();
    void usageFilterChanged();
    void sortKeyChanged();
    void countChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    void updateSorting();

    QString m_searchText;
    QString m_usageFilter = QStringLiteral("all");
    QString m_sortKey = QStringLiteral("name");
};

} // namespace Kontainer
