/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/detail_list_model.h"

namespace Kontainer
{

DetailListModel::DetailListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int DetailListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return int(m_entries.size());
}

int DetailListModel::count() const
{
    return int(m_entries.size());
}

bool DetailListModel::empty() const
{
    return m_entries.isEmpty();
}

QVariant DetailListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) {
        return {};
    }
    const DetailEntry &entry = m_entries.at(index.row());
    switch (role) {
    case LabelRole:
        return entry.label;
    case ValueRole:
        return entry.value;
    case DetailRole:
        return entry.detail;
    case EntryKeyRole:
        return entry.entryKey;
    default:
        break;
    }
    return {};
}

QHash<int, QByteArray> DetailListModel::roleNames() const
{
    return {
        {LabelRole, "label"},
        {ValueRole, "value"},
        {DetailRole, "detail"},
        {EntryKeyRole, "entryKey"},
    };
}

void DetailListModel::setEntries(const QList<DetailEntry> &entries)
{
    beginResetModel();
    m_entries = entries;
    endResetModel();
    Q_EMIT countChanged();
}

} // namespace Kontainer
