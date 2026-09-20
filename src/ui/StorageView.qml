/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Docker disk usage (ARCH_V2 §23/§30 / ARCH_V3 §2.4).

    Data comes from the `/system/df` structured API. Visualization (stacked bar + legend) lives in
    components/StorageBar; this file only handles the heading, error isolation and the total.

    An unavailable section shows "—" rather than 0; a failure only affects this block.
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.kcm.docker as Kontainer

import "components" as Components

ColumnLayout {
    id: view

    required property var controller

    readonly property var storage: controller.storage

    /*! A segment was clicked (forwarded to the page: the volumes segment opens the volume list). */
    signal segmentActivated(string entryKey)

    spacing: Kirigami.Units.smallSpacing

    function sizeText(bytes) {
        const text = Kontainer.Format.byteSize(bytes);
        return text.length > 0 ? text : i18n("—");
    }

    RowLayout {
        Layout.fillWidth: true

        QQC2.Label {
            text: i18n("Storage")
            font.bold: true
        }
        Item {
            Layout.fillWidth: true
        }
        QQC2.BusyIndicator {
            running: view.controller.storageStateKey === "loading"
            visible: running
            implicitHeight: Kirigami.Units.iconSizes.small
            implicitWidth: implicitHeight
        }
    }

    Kirigami.InlineMessage {
        Layout.fillWidth: true
        visible: view.controller.storageStateKey === "error"
        type: Kirigami.MessageType.Warning
        text: i18n("Unable to read Docker disk usage: %1", view.controller.storageError)
        actions: [
            Kirigami.Action {
                text: i18n("Retry")
                icon.name: "view-refresh"
                onTriggered: view.controller.retryStorage()
            }
        ]
    }

    Components.StorageBar {
        Layout.fillWidth: true
        visible: view.controller.storageStateKey !== "error"
        storage: view.storage
    }

    Kirigami.Separator {
        Layout.fillWidth: true
        visible: view.controller.storageStateKey !== "error"
    }

    RowLayout {
        Layout.fillWidth: true
        visible: view.controller.storageStateKey !== "error"

        QQC2.Label {
            Layout.fillWidth: true
            text: i18n("Total")
            font.bold: true
        }
        QQC2.Label {
            text: view.sizeText(view.storage.totalBytes)
            font.bold: true
            horizontalAlignment: Text.AlignRight
        }
    }
}
