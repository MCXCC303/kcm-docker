/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "model/row_sync_plan.h"

#include <QAbstractListModel>
#include <QList>

namespace Kontainer
{

/*!
 * Base class for list models that update incrementally by stable key (ARCH_V5_V8 §UX fixes).
 *
 * Why a base class: `beginInsertRows()` / `beginMoveRows()` are **protected** members of
 * `QAbstractItemModel`, callable only from a derived class — a free function has no access even
 * with a model pointer. So applying the plan lives here, and concrete models only supply
 * role/data (the algorithm stays in the unit-testable `planRowSync()`).
 *
 * Fixes (reported by users): clicking start/stop or returning from the detail page scrolled the
 * list back to the top; every refresh used `beginResetModel()`, and a model reset always resets
 * the ListView scroll position.
 */
template<typename Derived, typename T>
class KeyedListModel : public QAbstractListModel
{
public:
    using QAbstractListModel::QAbstractListModel;

protected:
    /*!
     * Incrementally sync `incoming` into `storage` by stable key.
     *
     * Only a real change of the key sequence (row count / order) touches the view; the same key
     * with a different value emits `dataChanged`, which does not move the view and leaves the
     * scroll position alone.
     *
     * @return whether anything changed (decides extra signals such as `countChanged`)
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

        // positions are settled: emit dataChanged for rows whose value changed (does not move the view)
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
