/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Docker disk usage（ARCH_V2 §23/§30）：Engine 级信息，数据来自 /system/df 结构化 API。
    某项不可用时显示 “—” 而不是 0；失败只影响本区块。
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.kontainer as Kontainer

ColumnLayout {
    id: view

    required property var controller

    readonly property var storage: controller.storage

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

    GridLayout {
        Layout.fillWidth: true
        visible: view.controller.storageStateKey !== "error"
        columns: 2
        columnSpacing: Kirigami.Units.largeSpacing
        rowSpacing: 0

        QQC2.Label {
            text: i18n("Images")
            opacity: 0.75
            Layout.fillWidth: true
        }
        QQC2.Label {
            text: view.sizeText(view.storage.imagesBytes)
            horizontalAlignment: Text.AlignRight
        }

        QQC2.Label {
            text: i18n("Containers")
            opacity: 0.75
            Layout.fillWidth: true
        }
        QQC2.Label {
            text: view.sizeText(view.storage.containersBytes)
            horizontalAlignment: Text.AlignRight
        }

        QQC2.Label {
            text: i18n("Volumes")
            opacity: 0.75
            Layout.fillWidth: true
        }
        QQC2.Label {
            text: view.sizeText(view.storage.volumesBytes)
            horizontalAlignment: Text.AlignRight
        }

        QQC2.Label {
            text: i18n("Build cache")
            opacity: 0.75
            visible: view.storage.buildCacheAvailable
            Layout.fillWidth: true
        }
        QQC2.Label {
            text: view.sizeText(view.storage.buildCacheBytes)
            visible: view.storage.buildCacheAvailable
            horizontalAlignment: Text.AlignRight
        }

        Kirigami.Separator {
            Layout.columnSpan: 2
            Layout.fillWidth: true
        }

        QQC2.Label {
            text: i18n("Total")
            font.bold: true
            Layout.fillWidth: true
        }
        QQC2.Label {
            text: view.sizeText(view.storage.totalBytes)
            font.bold: true
            horizontalAlignment: Text.AlignRight
        }
    }
}
