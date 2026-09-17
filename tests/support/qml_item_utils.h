/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QQuickItem>
#include <QString>
#include <QStringList>

namespace Kontainer::TestSupport
{

/*!
 * 在 QML 条目树里按 objectName 查找（测试助手）。
 *
 * **必须走可视树（childItems），不能用 QObject::findChildren**：
 * Repeater 创建的 delegate 的 QObject 父对象并不在页面的 QObject 树里
 * （实测 Qt 6.11：`page->findChildren<QQuickItem *>()` 看不到 delegate，
 * 而 `tileGrid->childItems()` 能看到），因此用 findChildren 写的断言会
 * 静默变成空操作——测试"通过"了，其实什么都没检查。
 */
inline QQuickItem *findItemByObjectName(QQuickItem *root, const QString &objectName)
{
    if (!root) {
        return nullptr;
    }
    const QList<QQuickItem *> children = root->childItems();
    for (QQuickItem *child : children) {
        if (child->objectName() == objectName) {
            return child;
        }
    }
    for (QQuickItem *child : children) {
        if (QQuickItem *found = findItemByObjectName(child, objectName)) {
            return found;
        }
    }
    return nullptr;
}

} // namespace Kontainer::TestSupport
