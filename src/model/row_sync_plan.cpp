/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/row_sync_plan.h"

namespace Kontainer
{

QList<RowOperation> planRowSync(const QStringList &currentKeys, const QStringList &incomingKeys)
{
    QList<RowOperation> plan;
    QStringList working = currentKeys;

    // ① removals: back to front, so earlier removals do not invalidate later indices
    for (int row = working.size() - 1; row >= 0; --row) {
        if (!incomingKeys.contains(working.at(row))) {
            plan.append({RowOperation::Kind::Remove, row, -1});
            working.removeAt(row);
        }
    }

    // ② place each key: the key at `index` in the target sequence must end up in row `index`
    for (int index = 0; index < incomingKeys.size(); ++index) {
        const QString &key = incomingKeys.at(index);
        const int current = int(working.indexOf(key));
        if (current < 0) {
            plan.append({RowOperation::Kind::Insert, -1, index});
            working.insert(index, key);
            continue;
        }
        if (current != index) {
            plan.append({RowOperation::Kind::Move, current, index});
            working.move(current, index);
        }
    }

    return plan;
}

} // namespace Kontainer
