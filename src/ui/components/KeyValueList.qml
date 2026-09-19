/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    「键 = 值」只读列表（ARCH_V3 §2.1）：

    渲染 DetailListModel（环境变量 / 标签 / 驱动选项 …）的 label + value 两列。

    实测反馈：「驱动选项」里选项名（`com.docker.network.bridge.name` 这种很长）**全被挡住**——
    原来给键列固定了 10 个 gridUnit 宽，长键被省略号吃掉。现在反过来：
    **键占剩余宽度**（真的放不下才省略），**值贴右对齐**（值通常很短）。

    调用方负责把它放进 Kirigami.FormLayout 之外的容器（例如 CollapsibleSection）。
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

ColumnLayout {
    id: list

    required property var model

    /*!
     * 值列的宽度上限（占整行比例）。
     *
     * 值通常很短（`true` / `172.18.0.0/16`），给个上限是为了让长值也不会把键挤没。
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

            // 键：占据剩余宽度（长键只有在真的放不下时才省略）
            QQC2.Label {
                objectName: "keyValueListKey"
                text: label
                font.family: "monospace"
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            // 值：贴右对齐，长度有上限，过长时中间省略
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
