/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "model/network_model.h"

#include <QSortFilterProxyModel>
#include <QString>

namespace Kontainer
{

/*!
 * 网络列表的搜索 / 过滤 / 排序代理（ARCH_V5_V8 §3.2）。
 *
 * 与容器/镜像列表同一套约定：
 *
 * - Search：名称 / Id / 短 Id / 驱动 / 子网 的大小写不敏感 substring 匹配
 * - Filter：All / 预定义 / 自定义（内置网络往往要藏起来，用户只关心自己建的）
 * - 排序：Name（默认，升序）/ driver / scope / members（成员数降序）
 * - 条件由代理自己持有：后台刷新（source reset）不会重置用户条件（§32）
 */
class NetworkFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT

    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
    /*! "all" / "predefined" / "custom" */
    Q_PROPERTY(QString originFilter READ originFilter WRITE setOriginFilter NOTIFY originFilterChanged)
    /*! "name" / "driver" / "scope" / "members" */
    Q_PROPERTY(QString sortKey READ sortKey WRITE setSortKey NOTIFY sortKeyChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    explicit NetworkFilterModel(QObject *parent = nullptr);

    QString searchText() const
    {
        return m_searchText;
    }
    void setSearchText(const QString &text);

    QString originFilter() const
    {
        return m_originFilter;
    }
    void setOriginFilter(const QString &filter);

    QString sortKey() const
    {
        return m_sortKey;
    }
    void setSortKey(const QString &key);

    int count() const;

Q_SIGNALS:
    void searchTextChanged();
    void originFilterChanged();
    void sortKeyChanged();
    void countChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    void updateSorting();

    QString m_searchText;
    QString m_originFilter = QStringLiteral("all");
    QString m_sortKey = QStringLiteral("name");
};

} // namespace Kontainer
