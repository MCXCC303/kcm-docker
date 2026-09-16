/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    镜像卡片（ARCH_V2 §6：整张卡片可点击进入 Image Detail；ARCH_V3 §2.1）。

    - 悬空镜像（0 引用）视觉弱化（ARCH_V3_pre §1.9），颜色统一走 StatusPalette
    - 复制入口：复制完整引用（repo:tag）；悬空镜像没有引用，退化为复制镜像 ID
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.kontainer as Kontainer

import "components" as Components

QQC2.ItemDelegate {
    id: card

    objectName: "imageCard"

    required property string imageId
    required property string shortId
    required property string primaryTag
    required property var tags
    required property real sizeBytes
    required property var created
    required property bool inUse
    required property bool dangling
    required property int containerCount

    signal activated

    width: ListView.view ? ListView.view.width : implicitWidth
    hoverEnabled: true

    onClicked: card.activated()

    /*! 复制目标：有引用时复制引用，悬空镜像复制完整 ID。 */
    readonly property string copyTarget: card.primaryTag.length > 0 ? card.primaryTag : card.imageId

    Accessible.name: card.dangling ? i18n("Dangling image %1", card.shortId) : i18n("Image %1", card.primaryTag)
    Accessible.role: Accessible.ListItem

    contentItem: RowLayout {
        spacing: Kirigami.Units.smallSpacing

        Kirigami.Icon {
            source: "image-x-generic"
            // 悬空镜像不是错误状态，只是「没有引用」，因此用中性语义而不是负向语义
            color: card.dangling ? Components.StatusPalette.color("neutral") : Kirigami.Theme.textColor
            implicitWidth: Kirigami.Units.iconSizes.smallMedium
            implicitHeight: Kirigami.Units.iconSizes.smallMedium
            Layout.alignment: Qt.AlignTop
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

            RowLayout {
                spacing: Kirigami.Units.smallSpacing

                QQC2.Label {
                    Layout.fillWidth: true
                    text: card.dangling ? i18n("<none>:<none> (dangling)") : card.primaryTag
                    font.bold: true
                    elide: Text.ElideMiddle
                }
                QQC2.Label {
                    visible: card.inUse
                    text: i18ncp("@info image is used by containers", "in use (%1)", "in use (%1)", card.containerCount)
                    color: Components.StatusPalette.color("positive")
                    font: Kirigami.Theme.smallFont
                }
            }

            QQC2.Label {
                Layout.fillWidth: true
                text: i18nc("@info image size, creation time and id", "%1 · created %2 ago · ID %3", Kontainer.Format.byteSize(card.sizeBytes), Kontainer.Format.elapsed(card.created), card.shortId)
                elide: Text.ElideRight
                font: Kirigami.Theme.smallFont
                opacity: 0.6
            }

            QQC2.Label {
                Layout.fillWidth: true
                visible: card.tags.length > 1
                text: i18nc("@info additional image tags", "Tags: %1", card.tags.join(", "))
                elide: Text.ElideRight
                font: Kirigami.Theme.smallFont
                opacity: 0.6
            }
        }

        // 列表里的复制入口（§1.3）：按钮自行接受鼠标事件，不会触发卡片导航
        Components.CopyButton {
            value: card.copyTarget
            fieldLabel: card.primaryTag.length > 0 ? i18n("image reference") : i18n("image ID")
            Layout.alignment: Qt.AlignVCenter
        }

        Kirigami.Icon {
            source: "go-next-symbolic"
            implicitWidth: Kirigami.Units.iconSizes.small
            implicitHeight: Kirigami.Units.iconSizes.small
            opacity: 0.5
            Layout.alignment: Qt.AlignVCenter
        }
    }
}
