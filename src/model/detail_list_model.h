/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "model/detail_entry.h"

#include <QAbstractListModel>
#include <QList>

namespace Kontainer
{

/*!
 * Generic read-only entry list model (ports / networks / mounts / labels / env vars / image layers /
 * related containers).
 *
 * Carries presentation data only; never accesses Docker or parses JSON.
 */
class DetailListModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countChanged)
    Q_PROPERTY(bool empty READ empty NOTIFY countChanged)
    /*!
     * Expose only the first N entries (0 = unlimited).
     *
     * Used for "image layers show the first 5 by default, all when expanded" (ARCH_V3 §2.3):
     * QAbstractListModel cannot be sliced from QML, and a view should not create delegates it never
     * shows, so the visible count is controlled here.
     */
    Q_PROPERTY(int limit READ limit WRITE setLimit NOTIFY limitChanged)

public:
    enum Roles {
        LabelRole = Qt::UserRole + 1,
        ValueRole,
        DetailRole,
        EntryKeyRole,
        /*! Related container's state key (icon and semantics). */
        StateKeyRole,
        /*! Click-through target (container id); empty = row not clickable. */
        TargetRole,
    };
    Q_ENUM(Roles)

    explicit DetailListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    /*! Total entry count (unaffected by limit). */
    int totalCount() const;
    bool empty() const;

    int limit() const
    {
        return m_limit;
    }
    void setLimit(int limit);

    void setEntries(const QList<DetailEntry> &entries);

Q_SIGNALS:
    void countChanged();
    void limitChanged();

private:
    QList<DetailEntry> m_entries;
    int m_limit = 0; /*!< 0 = unlimited */
};

} // namespace Kontainer
