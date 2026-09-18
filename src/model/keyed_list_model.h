/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "model/row_sync_plan.h"

#include <QAbstractListModel>
#include <QList>

namespace Kontainer
{

/*!
 * "按稳定键增量更新"的列表模型基类（ARCH_V5_V8 §体验修复）。
 *
 * 为什么需要基类：`beginInsertRows()` / `beginMoveRows()` 是 `QAbstractItemModel` 的
 * **protected** 成员，只有派生类自己能调；自由函数（哪怕传了模型指针）没有这个权限。
 * 因此把"执行计划"这一步放进基类，具体模型只提供 role/data（算法仍在可单测的
 * `planRowSync()` 里）。
 *
 * 解决的问题（用户实测）：点击启动/停止、或从详情页返回后，列表被拉回最上方——
 * 根因是原来每次刷新都 `beginResetModel()`，而模型重置必然让 ListView 跳回顶部。
 */
template<typename Derived, typename T>
class KeyedListModel : public QAbstractListModel
{
public:
    using QAbstractListModel::QAbstractListModel;

protected:
    /*!
     * 按稳定键把 `incoming` 增量同步到 `storage`。
     *
     * 只有键序列（行数 / 顺序）真的变了才动视图位置；键相同而值不同只发 `dataChanged`
     * ——后者不会移动视图，用户滚动到哪就停在哪。
     *
     * @return 是否有变化（决定要不要发 `countChanged` 之类的附加信号）
     */
    template<typename KeyFn, typename DiffersFn>
    bool syncRows(QList<T> &storage, const QList<T> &incoming, KeyFn keyOf, DiffersFn differs)
    {
        QStringList currentKeys;
        currentKeys.reserve(storage.size());
        for (const T &item : storage) {
            currentKeys.append(keyOf(item));
        }
        QStringList incomingKeys;
        incomingKeys.reserve(incoming.size());
        for (const T &item : incoming) {
            incomingKeys.append(keyOf(item));
        }

        const QList<RowOperation> plan = planRowSync(currentKeys, incomingKeys);

        for (const RowOperation &operation : plan) {
            switch (operation.kind) {
            case RowOperation::Kind::Remove:
                beginRemoveRows(QModelIndex(), operation.from, operation.from);
                storage.removeAt(operation.from);
                endRemoveRows();
                break;
            case RowOperation::Kind::Insert: {
                const T &item = incoming.at(operation.to);
                beginInsertRows(QModelIndex(), operation.to, operation.to);
                storage.insert(operation.to, item);
                endInsertRows();
                break;
            }
            case RowOperation::Kind::Move:
                beginMoveRows(QModelIndex(),
                              operation.from,
                              operation.from,
                              QModelIndex(),
                              operation.to > operation.from ? operation.to + 1 : operation.to);
                storage.move(operation.from, operation.to);
                endMoveRows();
                break;
            }
        }

        // 位置已经就位：值变了的行发 dataChanged（不移动视图）
        bool touched = !plan.isEmpty();
        const int rows = qMin(storage.size(), incoming.size());
        for (int row = 0; row < rows; ++row) {
            if (differs(storage.at(row), incoming.at(row))) {
                storage[row] = incoming.at(row);
                Q_EMIT static_cast<Derived *>(this)->dataChanged(index(row, 0), index(row, 0));
                touched = true;
            }
        }
        return touched;
    }
};

} // namespace Kontainer
