/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Overview 统计块（仪表盘）。

    取色与排版全部跟随主题：圆角用 Kirigami.Units.cornerRadius、背景用
    Kirigami.Theme.backgroundColor、边框由 textColor 派生、数值字号用
    Kirigami.Heading；语义 key → 颜色的映射统一在 StatusPalette（ARCH_V3 §2.1）。

    ## 为什么不用 Kirigami.AbstractCard（ARCH_V3 §四）

    §2.5 原本按 ARCH_V3_pre §1.3 把统计卡改成 AbstractCard，但实际会话中出现了
    段错误，core dump 的栈落在**嵌套布局的尺寸计算**上：

        qmlAttachedPropertiesObject ← QQuickLayoutAttached::sizeHint
        ← QGridLayoutEngine::fillRowData ← QQuickLayout::effectiveSizeHints_helper
        ← QQuickLayout::updatePolish

    AbstractCard 内部会再套三层布局（Padding → HeaderFooterLayout → Padding），
    并且用 `Connections` 在 contentItem 的 x/y 上挂了一个读取布局属性的
    `Qt.binding`（Kirigami 6.30 templates/AbstractCard.qml:110-140）。
    统计块是**静态展示**元素，不需要卡片的 hover/点击反馈，
    因此这里改回自绘容器：只保留一层内容布局，颜色仍全部取自主题，
    视觉与卡片一致但不再往 GridLayout 里塞嵌套布局树。
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

import "." as Local

Rectangle {
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

    // 圆角与边框跟随主题（与 Kirigami 卡片的观感一致）
    radius: Kirigami.Units.cornerRadius
    color: tile.tinted ? Local.StatusPalette.tintColor(tile.semanticKey) : Kirigami.Theme.backgroundColor
    border.width: 1
    border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15)

    HoverHandler {
        id: hoverHandler
    }

    QQC2.ToolTip.visible: tile.tooltip.length > 0 && hoverHandler.hovered
    QQC2.ToolTip.text: tile.tooltip
    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Kirigami.Units.largeSpacing
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

    Accessible.name: tile.label + ": " + tile.value
    Accessible.role: Accessible.Indicator
}
