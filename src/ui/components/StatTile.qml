/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Overview 统计块（仪表盘）。
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

Rectangle {
    id: tile

    required property string label
    required property string value
    required property string iconName
    property color accent: Kirigami.Theme.textColor
    property string tooltip

    implicitWidth: Kirigami.Units.gridUnit * 7
    implicitHeight: Kirigami.Units.gridUnit * 4.5
    radius: Kirigami.Units.smallSpacing
    color: Kirigami.Theme.alternateBackgroundColor
    border.width: 1
    border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15)

    QQC2.ToolTip.visible: tile.tooltip.length > 0 && hoverHandler.hovered
    QQC2.ToolTip.text: tile.tooltip
    HoverHandler {
        id: hoverHandler
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Kirigami.Units.smallSpacing
        spacing: 0

        RowLayout {
            spacing: Kirigami.Units.smallSpacing

            Kirigami.Icon {
                source: tile.iconName
                color: tile.accent
                implicitWidth: Kirigami.Units.iconSizes.small
                implicitHeight: Kirigami.Units.iconSizes.small
            }
            QQC2.Label {
                text: tile.label
                font: Kirigami.Theme.smallFont
                opacity: 0.75
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }

        Item {
            Layout.fillHeight: true
        }

        QQC2.Label {
            text: tile.value
            font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.8
            font.bold: true
            color: tile.accent
        }
    }

    Accessible.name: tile.label + ": " + tile.value
    Accessible.role: Accessible.Indicator
}
