/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Image card (ARCH_V2 §6: the whole card opens Image Detail; ARCH_V3 §2.1).

    - Dangling images (0 references) are visually de-emphasized (ARCH_V3_pre §1.9);
      all colors come from StatusPalette
    - Copy copies the full reference (repo:tag); dangling images have none, so it falls back to the image ID
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.kcm.docker as Kontainer

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

    /*! Copy target: the reference when one exists, otherwise the full image ID. */
    readonly property string copyTarget: card.primaryTag.length > 0 ? card.primaryTag : card.imageId

    Accessible.name: card.dangling ? i18n("Dangling image %1", card.shortId) : i18n("Image %1", card.primaryTag)
    Accessible.role: Accessible.ListItem

    contentItem: RowLayout {
        spacing: Kirigami.Units.smallSpacing

        Kirigami.Icon {
            source: "image-x-generic"
            // A dangling image is not an error, just unreferenced: neutral semantics, not negative
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

        // List copy entry (§1.3): the button accepts the mouse event itself, so it never triggers navigation
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
