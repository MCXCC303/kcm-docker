/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "model/container_model.h"

#include <QSortFilterProxyModel>
#include <QString>

namespace Kontainer
{

/*!
 * 容器列表的搜索 / 过滤 / 排序代理（ARCH_V2 §9/§10/§32）。
 *
 * - Search：name / ID / image 的大小写不敏感 substring 匹配（§9.2）
 * - Filter：All / Running / Paused / Stopped / Restarting / Dead（§9.3）
 * - 条件可组合：State == Running AND search matches（§9.4）
 * - 排序固定默认值：Name 升序（§10，写在注释与测试里）
 *
 * 代理自身持有搜索/过滤/排序状态，因此后台刷新（source model reset）不会重置用户条件（§32）。
 */
class ContainerFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT

    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
    /*! "all" / "running" / "paused" / "stopped" / "restarting" / "dead" */
    Q_PROPERTY(QString stateFilter READ stateFilter WRITE setStateFilter NOTIFY stateFilterChanged)
    /*! "name" / "state" / "created" */
    Q_PROPERTY(QString sortKey READ sortKey WRITE setSortKey NOTIFY sortKeyChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    explicit ContainerFilterModel(QObject *parent = nullptr);

    QString searchText() const
    {
        return m_searchText;
    }
    void setSearchText(const QString &text);

    QString stateFilter() const
    {
        return m_stateFilter;
    }
    void setStateFilter(const QString &filter);

    QString sortKey() const
    {
        return m_sortKey;
    }
    void setSortKey(const QString &key);

    int count() const;

Q_SIGNALS:
    void searchTextChanged();
    void stateFilterChanged();
    void sortKeyChanged();
    void countChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    void updateSorting();

    QString m_searchText;
    QString m_stateFilter = QStringLiteral("all");
    QString m_sortKey = QStringLiteral("name");
};

} // namespace Kontainer
