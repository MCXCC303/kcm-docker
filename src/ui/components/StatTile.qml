/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Overview 统计块（仪表盘）。

    - 基类是 Kirigami.AbstractCard 而不是自绘 Rectangle：
      圆角（Units.cornerRadius）、边框、内边距、阴影、View 配色集都跟随主题
      （ARCH_V3 §2.5 / ARCH_V3_pre §1.3）。
    - 颜色不在这里拼：语义 key → 颜色的映射统一在 StatusPalette（ARCH_V3 §2.1）。
      没有状态语义的统计项（容器总数、镜像数）semanticKey 留空，用普通文字色。
    - 数值字号用 Kirigami.Heading，不再手写 pointSize 倍数。
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

import "." as Local

Kirigami.AbstractCard {
    id: tile

    objectName: "statTile"

    required property string label
    required property string value
    required property string iconName

    /*! 状态语义 key（positive / neutral / negative）；空表示该项没有状态语义。 */
    property string semanticKey: ""

    /*!
        数值非零且带状态语义时，给卡片一层很淡的同色背景（ARCH_V3_pre §1.9
        「已停止卡片加背景色区分」）。计数为 0 时不着色：0 个已停止不是「问题」，
        不该把整张卡染红。三重编码（图标 + 颜色 + 文字）本身不因着色而退化。
    */
    property bool tintWhenNonZero: false

    property string tooltip

    readonly property color accentColor: tile.semanticKey.length > 0 ? Local.StatusPalette.color(tile.semanticKey) : Kirigami.Theme.textColor
    readonly property bool tinted: tile.tintWhenNonZero && tile.semanticKey.length > 0 && tile.value !== "0" && tile.value !== "—"

    implicitWidth: Kirigami.Units.gridUnit * 7
    implicitHeight: Kirigami.Units.gridUnit * 4.5

    contentItem: Item {
        implicitWidth: Kirigami.Units.gridUnit * 7 - tile.leftPadding - tile.rightPadding
        implicitHeight: Kirigami.Units.gridUnit * 4.5 - tile.topPadding - tile.bottomPadding

        // 着色层：向外扩到卡片边缘内 1px，这样卡片的主题边框仍然可见，
        // 圆角也与卡片一致（AbstractCard 的 contentItem 位于内边距之内）。
        Rectangle {
            anchors.fill: parent
            anchors.margins: -(tile.leftPadding - 1)
            z: -1
            visible: tile.tinted
            radius: Math.max(0, Kirigami.Units.cornerRadius - 1)
            color: Local.StatusPalette.tintColor(tile.semanticKey)
        }

        HoverHandler {
            id: hoverHandler
        }

        QQC2.ToolTip.visible: tile.tooltip.length > 0 && hoverHandler.hovered
        QQC2.ToolTip.text: tile.tooltip
        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            RowLayout {
                spacing: Kirigami.Units.smallSpacing

                Kirigami.Icon {
                    source: tile.iconName
                    color: tile.accentColor
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

            Kirigami.Heading {
                level: 1
                text: tile.value
                color: tile.accentColor
            }
        }
    }

    Accessible.name: tile.label + ": " + tile.value
    Accessible.role: Accessible.Indicator
}
