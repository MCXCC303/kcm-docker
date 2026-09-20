/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QList>
#include <QStringList>

namespace Kontainer
{

/*!
 * How one row should move (ARCH_V5_V8 §UX fixes).
 *
 * A plan rather than a direct model edit because `beginInsertRows()` and friends are **protected**
 * members of `QAbstractItemModel` that only the model (or a derived class) may call. This computes
 * "what to do" and each model executes it through its own protected API — one algorithm, testable
 * on its own.
 */
struct RowOperation {
    enum class Kind {
        Remove, /*!< remove the row at `from` */
        Insert, /*!< insert at `to` (data comes from the new list) */
        Move, /*!< move `from` to `to` */
    };

    Kind kind = Kind::Remove;
    int from = -1;
    int to = -1;

    friend bool operator==(const RowOperation &lhs, const RowOperation &rhs)
    {
        return lhs.kind == rhs.kind && lhs.from == rhs.from && lhs.to == rhs.to;
    }
};

/*!
 * Minimal operation sequence turning the current key sequence into the target one.
 *
 * The semantics directly drive the ListView experience:
 *   - **values only** (same key sequence) → empty plan: the model emits `dataChanged`, the view stays;
 *   - inserts, removals or moves → the view adjusts positions (necessary, and users understand it).
 *
 * Order: removals first (back to front, so indices stay valid), then place each key (moves before
 * inserts).
 */
QList<RowOperation> planRowSync(const QStringList &currentKeys, const QStringList &incomingKeys);

} // namespace Kontainer
