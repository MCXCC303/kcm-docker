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
    // 「有没有数据」与「当前显示几条」是两件事：折叠时 empty() 仍应为 false
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
    // 数据没变就什么都不做：不发 modelReset、不发 countChanged。
    // 详情页的列表由 5 秒（容器列表变化 → 关联容器）与 30 秒（inspect 复核）周期重建，
    // 无条件重置会让 Repeater 每次都销毁重建 delegate——「布局正在算尺寸时条目被销毁」
    // 正是真实会话里段错误的触发条件（ARCH_V3 附录 A.1d）。
    if (m_entries == entries) {
        return;
    }

    beginResetModel();
    m_entries = entries;
    endResetModel();
    Q_EMIT countChanged();
}

} // namespace Kontainer
