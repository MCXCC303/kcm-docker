/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QSortFilterProxyModel>

namespace Kontainer
{

/*!
 * 端口页的搜索 / 过滤 / 排序（ARCH_next_ports.md §4.A，里程碑 M3）。
 *
 * - 搜索：端口号、容器名、镜像、地址都能搜（用户想"8100 是谁占的"或"medai 用了哪些端口"）
 * - 过滤：全部 / 运行中占用（`inUse`）/ 声明未生效（`declaredNotPublished`）
 * - 排序：端口（默认，升序）/ 容器名
 *
 * 与其他过滤模型一样是代理模型：后台刷新不会重置用户的选择。
 */
class HostPortFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT

    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
    /*! "all" / "inUse" / "declaredNotPublished" */
    Q_PROPERTY(QString stateFilter READ stateFilter WRITE setStateFilter NOTIFY stateFilterChanged)
    /*! "port" / "container" */
    Q_PROPERTY(QString sortKey READ sortKey WRITE setSortKey NOTIFY sortKeyChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    explicit HostPortFilterModel(QObject *parent = nullptr);

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
    QString m_sortKey = QStringLiteral("port");
};

} // namespace Kontainer
