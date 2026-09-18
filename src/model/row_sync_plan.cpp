/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/row_sync_plan.h"

namespace Kontainer
{

QList<RowOperation> planRowSync(const QStringList &currentKeys, const QStringList &incomingKeys)
{
    QList<RowOperation> plan;
    QStringList working = currentKeys;

    // ① 删除：从后往前，前面的删除才不会让后面的索引失效
    for (int row = working.size() - 1; row >= 0; --row) {
        if (!incomingKeys.contains(working.at(row))) {
            plan.append({RowOperation::Kind::Remove, row, -1});
            working.removeAt(row);
        }
    }

    // ② 逐个就位：目标序列里第 index 个键，最终必须落在 index 这一行
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
