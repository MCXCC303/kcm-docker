/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QList>
#include <QStringList>

namespace Kontainer
{

/*!
 * 一行数据该怎么动（ARCH_V5_V8 §体验修复）。
 *
 * 之所以只做"计划"而不直接改模型：`beginInsertRows()` 这些是 `QAbstractItemModel` 的
 * **protected** 成员，只有模型自己（或其派生类）能调用。因此这里只算"要做什么"，
 * 由各模型用自己的 protected API 执行——算法仍然只有一份，也能单独测。
 */
struct RowOperation {
    enum class Kind {
        Remove, /*!< 删掉 `from` 这一行 */
        Insert, /*!< 在 `to` 位置插入（数据来自新列表） */
        Move, /*!< 把 `from` 移到 `to` */
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
 * 把"当前键序列"变成"目标键序列"所需的最小操作序列。
 *
 * 语义与 ListView 的体验直接相关：
 *   - **只改值**（键序列相同）→ 返回空计划：模型只需发 `dataChanged`，视图不动；
 *   - 有增删或换位 → 视图才会调整位置（这是必要的，用户也能理解）。
 *
 * 顺序规则：先删（从后往前，索引不串），再逐个就位（换位优先于插入）。
 */
QList<RowOperation> planRowSync(const QStringList &currentKeys, const QStringList &incomingKeys);

} // namespace Kontainer
