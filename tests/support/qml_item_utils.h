/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QQuickItem>
#include <QString>
#include <QStringList>

namespace Kontainer::TestSupport
{

/*!
 * Find an item by objectName in the QML item tree (test helper).
 *
 * **Must walk the visual tree (childItems), not QObject::findChildren**: a Repeater delegate's
 * QObject parent is not in the page's QObject tree (measured on Qt 6.11:
 * `page->findChildren<QQuickItem *>()` misses the delegate while `tileGrid->childItems()` sees
 * it), so assertions built on findChildren silently become no-ops — the test "passes" without
 * checking anything.
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
