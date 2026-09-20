/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Volume card (ARCH_V5_V8 §3.5): the whole card is clickable and opens the volume details.

    Size and reference count may be **unknown** (the engine has not scanned): unknown shows "—",
    not 0 — "0 bytes" and "no idea" are completely different answers for safe cleanup.
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.kcm.docker as Kontainer

import "components" as Components

QQC2.ItemDelegate {
    id: card

    objectName: "volumeCard"

    required property string name
    required property string driver
    required property string mountpoint
    required property bool sizeKnown
    required property double sizeBytes
    required property bool usageKnown
    required property int refCount

    signal activated

    width: ListView.view ? ListView.view.width : implicitWidth
    hoverEnabled: true
    onClicked: card.activated()

    Accessible.name: i18n("Volume %1", card.name)
    Accessible.role: Accessible.ListItem

    contentItem: RowLayout {
        spacing: Kirigami.Units.smallSpacing

        Kirigami.Icon {
            source: "drive-harddisk"
            // No color when usage is unknown (unknown is neither an error nor "idle")
            color: card.usageKnown && !card.refCount ? Components.StatusPalette.color("neutral") : Kirigami.Theme.textColor
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
                    objectName: "volumeNameLabel"
                    text: card.name
                    font.bold: true
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }

                Components.FieldChip {
                    objectName: "volumeDriverChip"
                    muted: true
                    text: card.driver
                }

                Components.FieldChip {
                    objectName: "volumeInUseChip"
                    visible: card.usageKnown && card.refCount > 0
                    muted: true
                    text: i18np("used by %1 container", "used by %1 containers", card.refCount)
                }

                Components.FieldChip {
                    objectName: "volumeUnusedChip"
                    visible: card.usageKnown && card.refCount === 0
                    muted: true
                    text: i18n("unused")
                }

                Components.FieldChip {
                    objectName: "volumeUnknownUsageChip"
                    visible: !card.usageKnown
                    muted: true
                    text: i18n("usage unknown")
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                QQC2.Label {
                    objectName: "volumeMountpointLabel"
                    Layout.fillWidth: true
                    text: card.mountpoint
                    font.family: "monospace"
                    opacity: 0.8
                    elide: Text.ElideMiddle
                }

                QQC2.Label {
                    objectName: "volumeSizeLabel"
                    // Unknown shows — (not 0)
                    text: card.sizeKnown ? Kontainer.Format.byteSize(card.sizeBytes) : i18n("—")
                    opacity: 0.8
                }
            }
        }
    }
}
