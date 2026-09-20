/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "model/volume_model.h"

#include <QSortFilterProxyModel>
#include <QString>

namespace Kontainer
{

/*!
 * Search / filter / sort proxy for the volume list (ARCH_V5_V8 §3.5).
 *
 * - Search: name / driver / mountpoint
 * - Filter: all / unused (`unused`; what prune targets) / in use (`inUse`)
 *   -- an unknown ref count is neither unused nor in use: it shows only under "all"
 *   and is labelled unknown (counting it as 0 would make users think it prunable)
 * - Sort: name (default) / driver / size (descending) / refs (descending)
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
