/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QSortFilterProxyModel>

namespace Kontainer
{

/*!
 * Search / filter / sort for the ports page (ARCH_next_ports.md §4.A, milestone M3).
 *
 * - Search: port number, container name, image, address (users ask "who holds 8100" or "which ports
 *   does medai use")
 * - Filter: all / in use (`inUse`) / declared but not published (`declaredNotPublished`)
 * - Sort: port (default, ascending) / container name
 *
 * A proxy model like the others: a background refresh never resets the user's choices.
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
