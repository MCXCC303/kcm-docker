/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Container card (ARCH_V2 §6 / ARCH_V3 §2.1 / ARCH_V4 §2.3): the whole card is one navigation target.

    - QQC2.ItemDelegate supplies hover / focus / Enter / Space behavior (§6.2/§38)
    - Only reversible actions inline (start / stop); delete stays in the detail footer,
      shrinking the misclick surface (ARCH_V4 §2.3)
    - Write actions appear only when the socket allows writes, and are disabled with a busy
      indicator while one is in flight
    - State uses the shared StatusChip (icon + color + text, §12)
    - Copy copies the identifier (container ID) only, never the whole inspect JSON (ARCH_V2 §41)
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.kcm.docker as Kontainer

import "components" as Components

QQC2.ItemDelegate {
    id: card

    objectName: "containerCard"

    /*! Model roles: without the `required` declaration these identifiers do not resolve inside the
        delegate, and clicking throws ReferenceError: containerId is not defined. */
    required property string containerId
    required property string name
    required property string shortId
    required property string image
    required property string stateKey
    required property string stateText
    required property string status
    required property string healthKey
    required property string healthText
    required property string portsSummary
    required property var created

    /*! Write-operation controller, passed in by MainPage: the component never looks up the KCM itself. */
    required property var operations

    signal activated

    width: ListView.view ? ListView.view.width : implicitWidth
    hoverEnabled: true

    onClicked: card.activated()

    Accessible.name: i18n("Container %1, %2", card.name, card.stateText)
    Accessible.description: card.image
    Accessible.role: Accessible.ListItem

    /*! State semantics: health outranks state, so an Unhealthy Running container must look wrong (§11.3) */
    readonly property string stateSemanticKey: Kontainer.Presentation.stateSemanticKey(card.stateKey, card.healthKey)
    readonly property bool healthVisible: card.healthKey === "healthy" || card.healthKey === "unhealthy" || card.healthKey === "starting"
    /*! Whether any operation is in flight for this container (start / stop / restart / delete). */
    readonly property bool targetBusy: {
        // Q_INVOKABLE calls are not tracked by QML: read a notifiable property first,
        // so this re-evaluates when busy changes
        card.operations.stateRevision;
        return card.operations.isContainerBusy(card.containerId);
    }
    readonly property bool canStart: card.operations.writeAllowed && !card.targetBusy
        && (card.stateKey === "exited" || card.stateKey === "created" || card.stateKey === "dead")
    readonly property bool canStop: card.operations.writeAllowed && !card.targetBusy
        && (card.stateKey === "running" || card.stateKey === "paused" || card.stateKey === "restarting")
    readonly property bool canPause: card.operations.writeAllowed && !card.targetBusy && card.stateKey === "running"
    readonly property bool canUnpause: card.operations.writeAllowed && !card.targetBusy && card.stateKey === "paused"

    contentItem: RowLayout {
        spacing: Kirigami.Units.smallSpacing

        Kirigami.Icon {
            source: Kontainer.Presentation.stateIconName(card.stateKey)
            color: Components.StatusPalette.color(card.stateSemanticKey)
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
                    objectName: "containerTitleLabel"
                    // Short ID in parentheses after the name: user testing found the old
                    // "uptime · created … · ID …" line too cramped, and the ID is the part people copy
                    text: i18nc("@info container name with short id", "%1 (%2)", card.name, card.shortId)
                    font.bold: true
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                Components.StatusChip {
                    semanticKey: card.stateSemanticKey
                    iconName: Kontainer.Presentation.stateIconName(card.stateKey)
                    text: card.stateText
                }

                RowLayout {
                    visible: card.healthVisible
                    spacing: Kirigami.Units.smallSpacing / 2

                    Kirigami.Icon {
                        source: Kontainer.Presentation.healthIconName(card.healthKey)
                        color: Components.StatusPalette.color(card.stateSemanticKey)
                        implicitWidth: Kirigami.Units.iconSizes.small
                        implicitHeight: Kirigami.Units.iconSizes.small
                    }
                    QQC2.Label {
                        text: card.healthText
                        font: Kirigami.Theme.smallFont
                        opacity: 0.8
                    }
                }
            }

            QQC2.Label {
                Layout.fillWidth: true
                text: card.image
                elide: Text.ElideMiddle
                font: Kirigami.Theme.smallFont
                opacity: 0.8
            }

            // Uptime, creation time and port mappings are gone from here: user testing found the
            // list too dense. The container detail page shows all of them, more completely.
        }

        // Inline lifecycle actions (ARCH_V4 §2.3): reversible in the list, destructive in the detail page.
        // The buttons accept mouse events themselves, so they never trigger card navigation.
        QQC2.BusyIndicator {
            objectName: "containerBusyIndicator"
            visible: card.targetBusy
            running: card.targetBusy
            implicitWidth: Kirigami.Units.iconSizes.smallMedium
            implicitHeight: Kirigami.Units.iconSizes.smallMedium
            Layout.alignment: Qt.AlignVCenter
        }

        QQC2.ToolButton {
            objectName: "containerStartButton"
            visible: card.canStart
            icon.name: "media-playback-start"
            text: i18n("Start")
            display: QQC2.AbstractButton.IconOnly
            Layout.alignment: Qt.AlignVCenter
            Accessible.name: i18n("Start container %1", card.name)
            QQC2.ToolTip.text: i18n("Start")
            QQC2.ToolTip.visible: hovered
            onClicked: card.operations.startContainer(card.containerId)
        }

        QQC2.ToolButton {
            objectName: "containerPauseButton"
            visible: card.canPause
            icon.name: "media-playback-pause"
            text: i18n("Pause")
            display: QQC2.AbstractButton.IconOnly
            Layout.alignment: Qt.AlignVCenter
            Accessible.name: i18n("Pause container %1", card.name)
            QQC2.ToolTip.text: i18n("Pause")
            QQC2.ToolTip.visible: hovered
            onClicked: card.operations.pauseContainer(card.containerId)
        }

        QQC2.ToolButton {
            objectName: "containerUnpauseButton"
            visible: card.canUnpause
            icon.name: "media-playback-start"
            text: i18n("Resume")
            display: QQC2.AbstractButton.IconOnly
            Layout.alignment: Qt.AlignVCenter
            Accessible.name: i18n("Resume container %1", card.name)
            QQC2.ToolTip.text: i18n("Resume")
            QQC2.ToolTip.visible: hovered
            onClicked: card.operations.unpauseContainer(card.containerId)
        }

        QQC2.ToolButton {
            objectName: "containerStopButton"
            visible: card.canStop
            icon.name: "media-playback-stop"
            text: i18n("Stop")
            display: QQC2.AbstractButton.IconOnly
            Layout.alignment: Qt.AlignVCenter
            Accessible.name: i18n("Stop container %1", card.name)
            QQC2.ToolTip.text: i18n("Stop")
            QQC2.ToolTip.visible: hovered
            onClicked: card.operations.stopContainer(card.containerId)
        }

        // List copy entry (§1.3): copies the container ID. Clicking here does not navigate, because
        // AbstractButton swallows the mouse event instead of letting it reach the parent delegate.
        Components.CopyButton {
            value: card.containerId
            fieldLabel: i18n("container ID")
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
