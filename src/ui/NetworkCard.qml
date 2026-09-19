/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    网络卡片（ARCH_V5_V8 §3.2）：整张卡片可点击进入网络详情。

    内置网络（`bridge` / `host` / `none`）用中性芯片标注：它们**删不掉**
    （daemon 回 403 `is a pre-defined network`），界面在列表里就把这件事说清楚，
    而不是等用户点删除才失败。
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.kcm.docker as Kontainer

import "components" as Components

QQC2.ItemDelegate {
    id: card

    objectName: "networkCard"

    required property string id
    required property string shortId
    required property string name
    required property string driver
    required property string scope
    required property string subnet
    required property bool predefined
    required property bool internal
    required property int memberCount
    required property var created

    signal activated

    width: ListView.view ? ListView.view.width : implicitWidth
    hoverEnabled: true
    onClicked: card.activated()

    Accessible.name: i18n("Network %1", card.name)
    Accessible.role: Accessible.ListItem

    contentItem: RowLayout {
        spacing: Kirigami.Units.smallSpacing

        Kirigami.Icon {
            source: "network-workgroup"
            color: card.internal ? Components.StatusPalette.color("neutral") : Kirigami.Theme.textColor
            implicitWidth: Kirigami.Units.iconSizes.smallMedium
            implicitHeight: Kirigami.Units.iconSizes.smallMedium
            Layout.alignment: Qt.AlignTop
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing / 2

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                QQC2.Label {
                    objectName: "networkNameLabel"
                    text: card.name
                    font.bold: true
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }

                Components.FieldChip {
                    objectName: "networkDriverChip"
                    muted: true
                    text: card.driver
                }

                Components.FieldChip {
                    objectName: "networkPredefinedChip"
                    visible: card.predefined
                    muted: true
                    text: i18n("built-in")
                }

                Components.FieldChip {
                    objectName: "networkInternalChip"
                    visible: card.internal
                    muted: true
                    text: i18n("internal")
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                Components.CopyableText {
                    objectName: "networkShortId"
                    value: card.shortId
                    fieldLabel: i18n("network ID")
                }

                QQC2.Label {
                    objectName: "networkSubnetLabel"
                    visible: card.subnet.length > 0
                    text: card.subnet
                    font.family: "monospace"
                    opacity: 0.8
                    elide: Text.ElideRight
                }

                Item {
                    Layout.fillWidth: true
                }

                QQC2.Label {
                    objectName: "networkMembersLabel"
                    // 成员数为 0 时也显示：这是"这个网络有没有在用"的直接信号
                    text: i18ncp("@info network member count", "%1 container", "%1 containers", card.memberCount)
                    opacity: 0.8
                }

                Components.CopyButton {
                    objectName: "networkCopyIdButton"
                    value: card.id
                    fieldLabel: i18n("network ID")
                }
            }
        }
    }
}
