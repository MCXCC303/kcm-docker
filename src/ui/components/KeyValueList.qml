/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Read-only "key = value" list (ARCH_V3 §2.1):

    Renders the label + value columns of a DetailListModel (environment variables /
    labels / driver options …).

    Measured feedback: in "driver options", long names such as
    `com.docker.network.bridge.name` were **cut off everywhere** — the key column had a
    fixed 10 gridUnit width, so the ellipsis ate long keys. Inverted now: the **key takes
    the remaining width** (elided only when it truly does not fit) and the **value is
    right-aligned** (values are usually short).

    The caller puts it into a container other than Kirigami.FormLayout (e.g.
    CollapsibleSection).
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

ColumnLayout {
    id: list

    required property var model

    /*!
     * Upper bound for the value column width (share of the whole row).
     *
     * Values are usually short (`true` / `172.18.0.0/16`); the cap keeps a long value from
     * squeezing the key away.
     */
    property real valueWidthRatio: 0.45

    Layout.fillWidth: true
    spacing: 0

    Repeater {
        model: list.model

        delegate: RowLayout {
            required property string label
            required property string value
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            // Key: takes the remaining width (long keys are elided only when they truly do not fit)
            QQC2.Label {
                objectName: "keyValueListKey"
                text: label
                font.family: "monospace"
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            // Value: right-aligned, width capped, elided in the middle when too long
            QQC2.Label {
                objectName: "keyValueListValue"
                text: value
                font.family: "monospace"
                horizontalAlignment: Text.AlignRight
                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                Layout.maximumWidth: Math.max(Kirigami.Units.gridUnit * 4,
                                              list.width * list.valueWidthRatio)
                elide: Text.ElideMiddle
            }
        }
    }
}
