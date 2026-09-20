/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Overview statistic tile (dashboard).

    Colors and typography all follow the theme: corners use Kirigami.Units.cornerRadius,
    background Kirigami.Theme.backgroundColor, the border is derived from textColor, the
    value uses Kirigami.Heading; the semantic key → color map lives in StatusPalette
    (ARCH_V3 §2.1).

    ## Why not Kirigami.AbstractCard (ARCH_V3 §4)

    §2.5 first turned the stat cards into AbstractCards per ARCH_V3_pre §1.3, but real
    sessions then segfaulted, with the core dump stack in **nested layout size
    computation**:

        qmlAttachedPropertiesObject ← QQuickLayoutAttached::sizeHint
        ← QGridLayoutEngine::fillRowData ← QQuickLayout::effectiveSizeHints_helper
        ← QQuickLayout::updatePolish

    AbstractCard nests three more layouts inside (Padding → HeaderFooterLayout → Padding)
    and hangs a `Qt.binding` on the contentItem's x/y via `Connections` that reads layout
    properties (Kirigami 6.30 templates/AbstractCard.qml:110-140). Stat tiles are **static
    display** elements needing no card hover/click feedback, so this went back to a
    self-drawn container: one content layout only, colors still all from the theme, visually
    identical to a card but without another nested layout tree inside the GridLayout.
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

    /*! Status semantic key (positive / neutral / negative); empty means the tile has no status semantics. */
    property string semanticKey: ""

    /*!
        When the value is non-zero and carries status semantics, tint the tile with a very
        faint version of the color (ARCH_V3_pre §1.9 "tint stopped cards to set them apart").
        A count of 0 gets no tint: 0 stopped is not a "problem", so the tile must not be
        painted red. Triple encoding (icon + color + text) does not degrade when tinted.
    */
    property bool tintWhenNonZero: false

    property string tooltip

    readonly property color accentColor: tile.semanticKey.length > 0 ? Local.StatusPalette.color(tile.semanticKey) : Kirigami.Theme.textColor
    readonly property bool tinted: tile.tintWhenNonZero && tile.semanticKey.length > 0 && tile.value !== "0" && tile.value !== "—"

    implicitWidth: Kirigami.Units.gridUnit * 7
    implicitHeight: Kirigami.Units.gridUnit * 4.5

    // Corners and border follow the theme (matching the look of a Kirigami card)
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
