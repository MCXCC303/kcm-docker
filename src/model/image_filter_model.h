/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "model/image_model.h"

#include <QSortFilterProxyModel>
#include <QString>

namespace Kontainer
{

/*!
 * Search / filter / sort proxy for the image list (ARCH_V2 §9/§10).
 *
 * - Search: case-insensitive substring match on repository / tag / ID
 * - Filter: all / in-use / dangling (dangling = no usable tag)
 * - Default sort: Repository ascending (§10)
 */
class ImageFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT

    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
    /*! "all" / "in-use" / "dangling" */
    Q_PROPERTY(QString useFilter READ useFilter WRITE setUseFilter NOTIFY useFilterChanged)
    /*! "repository" / "created" / "size" */
    Q_PROPERTY(QString sortKey READ sortKey WRITE setSortKey NOTIFY sortKeyChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    explicit ImageFilterModel(QObject *parent = nullptr);

    QString searchText() const
    {
        return m_searchText;
    }
    void setSearchText(const QString &text);

    QString useFilter() const
    {
        return m_useFilter;
    }
    void setUseFilter(const QString &filter);

    QString sortKey() const
    {
        return m_sortKey;
    }
    void setSortKey(const QString &key);

    int count() const;

Q_SIGNALS:
    void searchTextChanged();
    void useFilterChanged();
    void sortKeyChanged();
    void countChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    void updateSorting();

    QString m_searchText;
    QString m_useFilter = QStringLiteral("all");
    QString m_sortKey = QStringLiteral("repository");
};

} // namespace Kontainer
