/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "model/network_model.h"

#include <QSortFilterProxyModel>
#include <QString>

namespace Kontainer
{

/*!
 * Search / filter / sort proxy for the network list (ARCH_V5_V8 §3.2).
 *
 * Same conventions as the container/image lists:
 *
 * - Search: case-insensitive substring match on name / Id / short Id / driver / subnet
 * - Filter: All / predefined / custom (built-in networks are usually hidden; users want their own)
 * - Sort: Name (default, ascending) / driver / scope / members (member count descending)
 * - The proxy owns the criteria, so a background source reset never drops the user's filter (§32)
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
