/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Host port list (ARCH_next_ports.md §4.A, milestone M3).

    One row = one host port + who uses it + which container port it maps to.
    Three states (wording fixed after user testing, short enough to read at a glance):
      - `inUse`                 Running: a running container **really** publishes it;
      - `declaredNotPublished`  Not bound: the container runs, but the declared mapping never
                                took effect (`--net=host` and friends make `-p` a no-op);
      - `reserved`              Not started: the container is down, the port is free now,
                                but it will be claimed back once the container starts.

    Layout (from user testing): keep exactly **one** scrollbar — use the ScrollView's own
    ScrollBar instead of writing another; and the header no longer covers the first row
    (laid out separately with ColumnLayout, with its own background colour).

    Interaction (from user testing): the **whole row** is clickable to open the container
    detail; the "open/stop" buttons are gone (stopping lives on the detail page).
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

import "." as Local

ColumnLayout {
    id: root

    /*! Filtered model (`HostPortFilterModel`). */
    required property var model

    /*! Whole-row click: request the container detail. */
    signal containerRequested(string containerId, string containerName)

    objectName: "hostPortListView"
    spacing: Kirigami.Units.smallSpacing

    /* Column widths are defined once here: header and rows share them, one edit moves both */
    readonly property real portWidth: Kirigami.Units.gridUnit * 7
    readonly property real addressWidth: Kirigami.Units.gridUnit * 13
    readonly property real mappingWidth: Kirigami.Units.gridUnit * 9
    readonly property real stateWidth: Kirigami.Units.gridUnit * 7
    readonly property real imageWidth: Kirigami.Units.gridUnit * 12

    /*! State key → semantic colour / icon / text; colour alone never carries meaning. */
    function semanticKeyFor(stateKey: string): string {
        switch (stateKey) {
        case "inUse":
            return "positive";
        case "reservedTaken":
            return "negative";
        case "reserved":
            return "neutral";
        case "declaredNotPublished":
            // One notch weaker than "not started" (user requirement: not started > not bound)
            return "disabled";
        default:
            return "neutral";
        }
    }
    function iconNameFor(stateKey: string): string {
        switch (stateKey) {
        case "inUse":
            return "media-playback-start";
        case "declaredNotPublished":
        case "reservedTaken":
            return "dialog-warning";
        default:
            return "dialog-information";
        }
    }
    /*! State text: short (Running / Not bound / Not started); the full meaning goes in the tooltip. */
    function stateText(stateKey: string): string {
        switch (stateKey) {
        case "inUse":
            return i18n("Running");
        case "declaredNotPublished":
            return i18n("Not bound");
        case "reservedTaken":
            // Port already taken by someone else: this container hits a conflict when it starts
            return i18n("Taken");
        default:
            return i18n("Not started");
        }
    }
    function stateHint(stateKey: string): string {
        switch (stateKey) {
        case "inUse":
            return i18n("A running container publishes this port.");
        case "declaredNotPublished":
            // Wording given by the user
            return i18n("The container is running, but this port mapping did not take effect.");
        case "reservedTaken":
            return i18n("This port is currently used by another container, so this container will fail to start with a port conflict.");
        default:
            return i18n("This container needs to publish this port when it starts.");
        }
    }

    /*!
     * Header (user testing: the columns were unclear and column two was always —).
     *
     * Has a background (the theme's alternate background colour) and is laid out
     * **separately** from the list — with anchors the header used to cover the first row,
     * looking like a mixed-up layer.
     */
    Rectangle {
        id: headerBackground

        objectName: "hostPortHeaderBackground"
        Layout.fillWidth: true
        implicitHeight: header.implicitHeight + Kirigami.Units.smallSpacing * 2
        color: Kirigami.Theme.alternateBackgroundColor
        radius: 4

        RowLayout {
            id: header

            objectName: "hostPortHeader"
            anchors.fill: parent
            anchors.leftMargin: Kirigami.Units.smallSpacing
            anchors.rightMargin: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.smallSpacing

            QQC2.Label {
                objectName: "hostPortHeaderPort"
                Layout.preferredWidth: root.portWidth
                text: i18n("Host port")
                font.bold: true
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                opacity: 0.8
            }

            QQC2.Label {
                objectName: "hostPortHeaderAddress"
                Layout.preferredWidth: root.addressWidth
                text: i18n("Bind address")
                font.bold: true
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                opacity: 0.8
            }

            QQC2.Label {
                objectName: "hostPortHeaderMapping"
                Layout.preferredWidth: root.mappingWidth
                text: i18n("Container port")
                font.bold: true
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                opacity: 0.8
            }

            QQC2.Label {
                objectName: "hostPortHeaderState"
                Layout.preferredWidth: root.stateWidth
                text: i18n("State")
                font.bold: true
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                opacity: 0.8
            }

            QQC2.Label {
                objectName: "hostPortHeaderContainer"
                Layout.fillWidth: true
                text: i18n("Container")
                font.bold: true
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                opacity: 0.8
            }

            QQC2.Label {
                objectName: "hostPortHeaderImage"
                Layout.preferredWidth: root.imageWidth
                text: i18n("Image")
                font.bold: true
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                opacity: 0.8
            }
        }
    }

    QQC2.ScrollView {
        id: scroll

        // Scroll only here (ScrollView brings its own ScrollBar; a hand-written one adds a second)
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true

        ListView {
            id: listView

            objectName: "hostPortList"
            model: root.model
            spacing: 0

            delegate: QQC2.ItemDelegate {
                id: row

                required property string portText
                required property string addressText
                required property bool wildcard
                required property int containerPort
                required property string protocol
                required property string stateKey
                required property string containerName
                required property string containerId
                required property string containerImage

                objectName: "hostPortRow"
                width: listView.width
                hoverEnabled: true
                // Whole row clickable = navigate (user requirement: no extra "open" button)
                onClicked: root.containerRequested(row.containerId, row.containerName)

                contentItem: RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    /* Port is the page's first visual focus (user rule: port first, container one column) */
                    QQC2.Label {
                        objectName: "hostPortRowPort"
                        Layout.preferredWidth: root.portWidth
                        text: row.portText
                        font.family: "monospace"
                        font.bold: true
                    }

                    QQC2.Label {
                        objectName: "hostPortRowAddress"
                        Layout.preferredWidth: root.addressWidth
                        // Wildcard no longer shows a dash: spell out "all interfaces" (also when v4+v6 merge)
                        text: row.wildcard ? i18n("All interfaces") : row.addressText
                        opacity: 0.75
                        elide: Text.ElideRight
                    }

                    QQC2.Label {
                        objectName: "hostPortRowMapping"
                        Layout.preferredWidth: root.mappingWidth
                        // Port/protocol is technical notation (80/tcp): not translated, no i18n wrapper
                        text: row.containerPort + "/" + row.protocol
                        font.family: "monospace"
                        opacity: 0.85
                    }

                    /*
                     * State column: a fixed width keeps it aligned with the header, but the
                     * **badge shrinks to its content** — giving the width to StatusChip
                     * stretches it and leaves a gap on the right (user report).
                     */
                    Item {
                        Layout.preferredWidth: root.stateWidth
                        Layout.fillHeight: true

                        Local.StatusChip {
                            objectName: "hostPortRowState"
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            semanticKey: root.semanticKeyFor(row.stateKey)
                            iconName: root.iconNameFor(row.stateKey)
                            text: root.stateText(row.stateKey)

                            // Short text + hover for the full meaning (user requirement: short states,
                            // no lost meaning). timeout is a fallback: stale attached tooltips still vanish.
                            QQC2.ToolTip.text: root.stateHint(row.stateKey)
                            QQC2.ToolTip.visible: hovered
                            QQC2.ToolTip.timeout: 5000
                        }
                    }

                    QQC2.Label {
                        objectName: "hostPortRowContainer"
                        Layout.fillWidth: true
                        Layout.minimumWidth: Kirigami.Units.gridUnit * 6
                        text: row.containerName
                        elide: Text.ElideRight
                    }

                    QQC2.Label {
                        objectName: "hostPortRowImage"
                        Layout.preferredWidth: root.imageWidth
                        Layout.maximumWidth: root.imageWidth
                        text: row.containerImage
                        opacity: 0.7
                        elide: Text.ElideMiddle
                    }

                    /* Whole-row-click hint: one icon only, no longer a button */
                    Kirigami.Icon {
                        objectName: "hostPortRowChevron"
                        source: "go-next-symbolic"
                        implicitWidth: Kirigami.Units.iconSizes.small
                        implicitHeight: Kirigami.Units.iconSizes.small
                        opacity: 0.6
                    }
                }
            }
        }
    }
}
