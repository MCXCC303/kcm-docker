/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    「键 = 值」只读列表（ARCH_V3 §2.1）：

    渲染 DetailListModel（环境变量 / 标签 / 端口 …）的 label + value 两列，
    键列等宽、值列等宽并中间省略。原本这段布局在两个详情页里各写了一遍。

    调用方负责把它放进 Kirigami.FormLayout 之外的容器（例如 CollapsibleSection）。
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

ColumnLayout {
    id: list

    required property var model

    /*! 键列宽度；等宽字体下保证多行对齐。 */
    property real keyWidth: Kirigami.Units.gridUnit * 10

    Layout.fillWidth: true
    spacing: 0

    Repeater {
        model: list.model

        delegate: RowLayout {
            required property string label
            required property string value
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            QQC2.Label {
                text: label
                font.family: "monospace"
                Layout.preferredWidth: list.keyWidth
                elide: Text.ElideRight
            }
            QQC2.Label {
                text: value
                font.family: "monospace"
                Layout.fillWidth: true
                elide: Text.ElideMiddle
            }
        }
    }
}
