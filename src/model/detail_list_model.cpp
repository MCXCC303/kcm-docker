/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
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
    return count();
}

int DetailListModel::count() const
{
    if (m_limit > 0 && m_limit < m_entries.size()) {
        return m_limit;
    }
    return int(m_entries.size());
}

int DetailListModel::totalCount() const
{
    return int(m_entries.size());
}

bool DetailListModel::empty() const
{
    // "Has data" and "how many are shown" are different: while collapsed empty() must stay false
    return m_entries.isEmpty();
}

void DetailListModel::setLimit(int limit)
{
    const int normalized = limit < 0 ? 0 : limit;
    if (normalized == m_limit) {
        return;
    }
    beginResetModel();
    m_limit = normalized;
    endResetModel();
    Q_EMIT limitChanged();
    Q_EMIT countChanged();
}

QVariant DetailListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= count()) {
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
    case StateKeyRole:
        return entry.stateKey;
    case TargetRole:
        return entry.target;
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
        {StateKeyRole, "stateKey"},
        {TargetRole, "target"},
    };
}

void DetailListModel::setEntries(const QList<DetailEntry> &entries)
{
    // Unchanged data: do nothing, no modelReset and no countChanged.
    // Detail lists are rebuilt every 5 s (container list change -> related containers) and every 30 s
    // (inspect recheck); an unconditional reset makes Repeater destroy and rebuild delegates each time --
    // "item destroyed while layout computes sizes" is what segfaulted real sessions (ARCH_V3 App. A.1d).
    if (m_entries == entries) {
        return;
    }

    beginResetModel();
    m_entries = entries;
    endResetModel();
    Q_EMIT countChanged();
}

} // namespace Kontainer
