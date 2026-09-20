/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Status and management of the Docker-related services (ARCH_V5_V8 §B1).

    User report: after `docker.service` was stopped this tool still showed "connected"
    (`docker.socket` was still up), and nothing offered a way to start the service again.
    Hence the state of all three units (socket / service / containerd) plus five
    **privilege-restricted** actions (start / stop / restart / enable / disable).

    Design points:
      - actions go through `controller.controlService(unit, verb)`: whitelist validation
        lives in C++ (an illegal request sends no privileged action)
      - stop / disable ask for **confirmation** and state the impact ("after stopping the
        socket this tool cannot connect")
      - status is a read-only query (systemd D-Bus); the controller re-queries after a success
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

import "." as Local

Kirigami.AbstractCard {
    id: root

    /*! Daemon settings controller of the engine page (also carries the service-control result channel). */
    required property var controller
    /*! Service state source (`kcm.controller.services`). */
    required property var services

    objectName: "serviceCard"
    showClickFeedback: false

    /*! (unit, verb) awaiting confirmation; the request is only sent after it. */
    property string pendingUnit: ""
    property string pendingVerb: ""

    /*! Unit name → UI text (the unit name itself is data and is never translated). */
    function unitLabel(unit: string): string {
        switch (unit) {
        case "docker.socket":
            return i18n("Docker socket");
        case "docker.service":
            return i18n("Docker service");
        case "containerd.service":
            return i18n("containerd service");
        default:
            return unit;
        }
    }

    /*! State key → text. */
    function stateText(stateKey: string): string {
        switch (stateKey) {
        case "running":
            return i18n("Running");
        case "stopped":
            return i18n("Stopped");
        case "failed":
            return i18n("Failed");
        case "starting":
            return i18n("Starting…");
        case "stopping":
            return i18n("Stopping…");
        default:
            return i18n("Unknown");
        }
    }

    /*! State key → color (consistent with the project's other status colors). */
    function stateColor(stateKey: string): color {
        switch (stateKey) {
        case "running":
            return Local.StatusPalette.color("positive");
        case "failed":
            return Local.StatusPalette.color("negative");
        case "stopped":
            return Local.StatusPalette.color("warning");
        default:
            // Unknown: do not guess a state, use the neutral color (mapped once in StatusPalette)
            return Local.StatusPalette.color("disabled");
        }
    }

    /*! Action key → text. */
    function verbText(verb: string): string {
        switch (verb) {
        case "start":
            return i18n("Start");
        case "stop":
            return i18n("Stop");
        case "restart":
            return i18n("Restart");
        case "enable":
            return i18n("Enable at boot");
        case "disable":
            return i18n("Disable at boot");
        default:
            return verb;
        }
    }

    /*! Dangerous actions (stop / disable) need confirmation. */
    function verbNeedsConfirmation(verb: string): bool {
        return verb === "stop" || verb === "disable";
    }

    /*! Request an action: dangerous ones ask first, the rest go straight out. */
    function requestAction(unit: string, verb: string): void {
        if (root.verbNeedsConfirmation(verb)) {
            root.pendingUnit = unit;
            root.pendingVerb = verb;
            confirmDialog.open();
            return;
        }
        root.controller.controlService(unit, verb);
    }

    /*! Consequence of a dangerous action (each spells out what will happen). */
    function consequenceText(unit: string, verb: string): string {
        if (verb === "stop" && unit === "docker.socket") {
            return i18n("Kontainer will not be able to reach the Docker daemon until the socket is started again.");
        }
        if (verb === "stop") {
            return i18n("Running containers of this daemon will be stopped.");
        }
        if (verb === "disable") {
            return i18n("The service will not be started automatically at boot.");
        }
        return "";
    }

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        Kirigami.Heading {
            Layout.fillWidth: true
            level: 3
            text: i18n("Services")
        }

        Kirigami.InlineMessage {
            objectName: "serviceResultMessage"
            Layout.fillWidth: true
            visible: root.controller.serviceErrorKey.length > 0
            type: Kirigami.MessageType.Error
            text: i18n("The service action failed: %1", root.controller.serviceErrorKey)
        }

        Repeater {
            model: root.services.services

            delegate: RowLayout {
                id: serviceRow

                required property var modelData

                objectName: "serviceRow"
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                QQC2.Label {
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 9
                    text: root.unitLabel(serviceRow.modelData.unit)
                    elide: Text.ElideRight
                }

                // State = dot + text (color is not the only differentiator, the text is there too)
                RowLayout {
                    objectName: "serviceStateChip"
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 6
                    spacing: Kirigami.Units.smallSpacing / 2

                    Rectangle {
                        objectName: "serviceStateDot"
                        implicitWidth: Kirigami.Units.smallSpacing * 1.5
                        implicitHeight: implicitWidth
                        radius: width / 2
                        color: root.stateColor(serviceRow.modelData.stateKey)
                        Accessible.ignored: true
                    }
                    QQC2.Label {
                        objectName: "serviceStateLabel"
                        text: root.stateText(serviceRow.modelData.stateKey)
                        elide: Text.ElideRight
                    }
                }

                QQC2.Label {
                    objectName: "serviceEnabledLabel"
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 10
                    visible: serviceRow.modelData.known
                    text: serviceRow.modelData.enabled ? i18n("Enabled at boot") : i18n("Not enabled at boot")
                    font: Kirigami.Theme.smallFont
                    opacity: 0.75
                    elide: Text.ElideRight
                }

                Item {
                    Layout.fillWidth: true
                }

                // Start / stop: shown by current state (running offers "stop", otherwise "start")
                QQC2.Button {
                    objectName: "serviceStartButton"
                    visible: serviceRow.modelData.known && !serviceRow.modelData.active
                    text: root.verbText("start")
                    icon.name: "media-playback-start"
                    enabled: !root.controller.serviceInFlight
                    onClicked: root.requestAction(serviceRow.modelData.unit, "start")
                }
                QQC2.Button {
                    objectName: "serviceStopButton"
                    visible: serviceRow.modelData.known && serviceRow.modelData.active
                    text: root.verbText("stop")
                    icon.name: "media-playback-stop"
                    enabled: !root.controller.serviceInFlight
                    onClicked: root.requestAction(serviceRow.modelData.unit, "stop")
                }
                QQC2.Button {
                    objectName: "serviceRestartButton"
                    visible: serviceRow.modelData.known && serviceRow.modelData.active
                    text: root.verbText("restart")
                    icon.name: "view-refresh"
                    enabled: !root.controller.serviceInFlight
                    onClicked: root.requestAction(serviceRow.modelData.unit, "restart")
                }
                QQC2.Button {
                    objectName: "serviceEnableButton"
                    visible: serviceRow.modelData.known && !serviceRow.modelData.enabled
                    text: root.verbText("enable")
                    icon.name: "system-run"
                    enabled: !root.controller.serviceInFlight
                    onClicked: root.requestAction(serviceRow.modelData.unit, "enable")
                }
                QQC2.Button {
                    objectName: "serviceDisableButton"
                    visible: serviceRow.modelData.known && serviceRow.modelData.enabled
                    text: root.verbText("disable")
                    icon.name: "dialog-cancel"
                    enabled: !root.controller.serviceInFlight
                    onClicked: root.requestAction(serviceRow.modelData.unit, "disable")
                }

                QQC2.BusyIndicator {
                    objectName: "serviceBusyIndicator"
                    visible: root.controller.serviceInFlight
                    running: root.controller.serviceInFlight
                    implicitWidth: Kirigami.Units.iconSizes.smallMedium
                    implicitHeight: Kirigami.Units.iconSizes.smallMedium
                }
            }
        }

        QQC2.Label {
            objectName: "serviceUnavailableHint"
            Layout.fillWidth: true
            visible: {
                const list = root.services.services;
                return list.length === 0 || !list[0].known;
            }
            text: i18n("The state of these services cannot be read (systemd is not reachable).")
            font: Kirigami.Theme.smallFont
            opacity: 0.75
            wrapMode: Text.WordWrap
        }
    }

    Local.ConfirmDialog {
        id: confirmDialog

        objectName: "serviceConfirmDialog"
        headingText: root.verbText(root.pendingVerb)
        questionText: i18n("%1 “%2”?", root.verbText(root.pendingVerb), root.unitLabel(root.pendingUnit))
        consequenceText: root.consequenceText(root.pendingUnit, root.pendingVerb)
        acceptText: root.verbText(root.pendingVerb)
        destructive: true
        onConfirmed: root.controller.controlService(root.pendingUnit, root.pendingVerb)
    }
}
