/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "model/container_model.h"

#include <QSortFilterProxyModel>
#include <QString>

namespace Kontainer
{

/*!
 * Search / filter / sort proxy for the container list (ARCH_V2 §9/§10/§32).
 *
 * - Search: case-insensitive substring match on name / ID / image (§9.2)
 * - Filter: All / Running / Paused / Stopped / Restarting / Dead (§9.3)
 * - Conditions combine: State == Running AND search matches (§9.4)
 * - Sort fixed at the default: Name ascending (§10, stated here and in tests)
 *
 * The proxy owns the search/filter/sort state, so a background refresh (source model reset) does not
 * reset the user's conditions (§32).
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
