/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Port mapping editor (node-graph style + inline conflict hints).

    Ports are filled in when creating a container; user feedback: "use the node graph
    style here too, and label host/container". PortTopology is **read-only display**;
    this is the **editable** version of that same visual language — host (IP + port)
    on the left, a dotted link in the middle, container port on the right, labels on top.

    Inline hints (user feedback): if a host port is taken by a **running** container or
    duplicates another row of the same form, say so directly under that row and offer a
    one-click "Use port N" — do not wait for
    `Bind for 0.0.0.0:8100 failed: port is already allocated` at start time.
    **Show nothing while idle**; ports declared by stopped containers are not conflicts
    (a stopped container holds no port).

    Why row edits go through the controller: under `pragma ComponentBehavior: Unbound`
    a delegate cannot reach the root object id (see CreateContainer.qml), so write-back
    only happens through `editor`, a **non-root** intermediary.

    Usage:

        Components.PortMappingEditor {
            controller: page.controller
            Layout.fillWidth: true
        }
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

import "." as Local

ColumnLayout {
    id: root

    /*! Create-wizard controller (provides addPortRow/setPortRow/removePortRow and portRowStatuses). */
    required property var controller

    objectName: "portMappingEditor"
    spacing: Kirigami.Units.smallSpacing

    /*! Intermediary for delegates: under Unbound a delegate cannot reference the root id. */
    QtObject {
        id: editor

        readonly property var controller: root.controller

        function add() {
            editor.controller.addPortRow(80, 0, "", "tcp");
        }
        function setField(row, field, value) {
            editor.controller.setPortRow(row, field, value);
        }
        function remove(row) {
            editor.controller.removePortRow(row);
        }
    }

    ListModel {
        id: portRows
    }

    /*! Inline status key → text (C++ supplies stable keys only, texts live in QML). */
    function portStatusText(status): string {
        switch (String(status.errorKey ?? "")) {
        case "portInUse":
            return i18n("Host port %1 is already used by “%2”.", status.hostPort ?? 0, status.holder ?? "");
        case "portDuplicateInRequest":
            return i18n("This host port is already used by another row of this form.");
        case "portRequired":
            return i18n("Enter a container port.");
        case "portRange":
            return i18n("The host port must be between 0 and 65535.");
        default:
            return "";
        }
    }

    /*! Text of the "Use port N" button (empty when there is no suggestion → no button). */
    function portSuggestionText(status): string {
        const suggestion = status ? Number(status.suggestion ?? 0) : 0;
        return suggestion > 0 ? i18n("Use port %1", suggestion) : "";
    }

    /*
     * Controller → edit buffer.
     *
     * **Update in place**; never clear()+append() while the row count is unchanged:
     * rebuilding the ListModel destroys the delegate being typed in (focus and cursor
     * are lost), which itself interrupts input. Rebuild only when rows are added or
     * removed, which is unavoidable.
     */
    function syncRows(): void {
        const rows = editor.controller.portRows;
        if (portRows.count === rows.length) {
            for (let i = 0; i < rows.length; ++i) {
                const row = rows[i];
                const item = portRows.get(i);
                if (item.containerPort !== (row.containerPort ?? 0)) {
                    portRows.setProperty(i, "containerPort", row.containerPort ?? 0);
                }
                if (item.hostPort !== (row.hostPort ?? 0)) {
                    portRows.setProperty(i, "hostPort", row.hostPort ?? 0);
                }
                if (item.hostIp !== (row.hostIp ?? "")) {
                    portRows.setProperty(i, "hostIp", row.hostIp ?? "");
                }
                if (item.protocol !== (row.protocol ?? "tcp")) {
                    portRows.setProperty(i, "protocol", row.protocol ?? "tcp");
                }
            }
            return;
        }
        portRows.clear();
        for (const row of rows) {
            portRows.append({
                containerPort: row.containerPort ?? 0,
                hostPort: row.hostPort ?? 0,
                hostIp: row.hostIp ?? "",
                protocol: row.protocol ?? "tcp"
            });
        }
    }

    Connections {
        target: root.controller
        function onChanged() {
            root.syncRows();
        }
    }

    Component.onCompleted: root.syncRows()

    /* ---------------------------- Labels for both sides ---------------------------- */
    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            objectName: "portEditorHostLabel"
            Layout.preferredWidth: Kirigami.Units.gridUnit * 12
            text: i18n("Host")
            font.bold: true
            opacity: 0.8
        }

        Item {
            Layout.fillWidth: true
        }

        QQC2.Label {
            objectName: "portEditorContainerLabel"
            Layout.preferredWidth: Kirigami.Units.gridUnit * 7
            text: i18n("Container")
            font.bold: true
            opacity: 0.8
            horizontalAlignment: Text.AlignRight
        }
    }

    Repeater {
        model: portRows

        delegate: ColumnLayout {
            id: portRowItem

            required property int index
            required property int containerPort
            required property int hostPort
            required property string hostIp
            required property string protocol

            objectName: "wizardPortRow"
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing / 2

            /*! This row's status (from `portRowStatuses`; `errorKey` is empty while idle). */
            readonly property var rowStatus: {
                const statuses = editor.controller.portRowStatuses;
                return portRowItem.index < statuses.length ? statuses[portRowItem.index] : null;
            }
            /*!
             * Whether this row has a conflict/error.
             *
             * Note that an out-of-bounds `statuses[index]` is `undefined`, not `null`:
             * testing `!== null` alone then throws on `undefined.errorKey` during early
             * layout (hit in practice), hence this single boolean.
             */
            readonly property bool hasStatusError: {
                const status = portRowItem.rowStatus;
                return !!status && String(status.errorKey ?? "").length > 0;
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                /* ---- Host side: IP + port (0 = random) ---- */
                QQC2.TextField {
                    objectName: "wizardPortHostIp"
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 5
                    placeholderText: "0.0.0.0"
                    Accessible.name: i18n("Host address")
                    text: portRowItem.hostIp
                    onTextEdited: editor.setField(portRowItem.index, "hostIp", text)
                }

                /*
                 * Host port is a **validated TextField**, not a SpinBox.
                 *
                 * User report: a SpinBox reflows its text on every external value change
                 * (pushing the cursor to the end), so typing "8000" became 8 → reselect →
                 * 0 → reselect… A TextField instead:
                 *  - does **not** write back to the controller while typing (only on
                 *    editingFinished), so no "model → text" loop can steal the cursor;
                 *  - validates with IntValidator (0…65535; empty/0 = random assignment).
                 */
                QQC2.TextField {
                    objectName: "wizardHostPort"
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 6
                    placeholderText: i18n("random")
                    Accessible.name: i18n("Host port")
                    horizontalAlignment: TextInput.AlignRight
                    inputMethodHints: Qt.ImhDigitsOnly
                    text: portRowItem.hostPort === 0 ? "" : String(portRowItem.hostPort)
                    validator: IntValidator {
                        bottom: 0
                        top: 65535
                    }
                    // Write back on editingFinished only: leaving the model alone keeps the cursor
                    onEditingFinished: editor.setField(portRowItem.index, "hostPort", text.length === 0 ? 0 : parseInt(text, 10))
                }

                /* ---- Middle link: same visual language as the port topology (socket dots at both ends) ----
                   The user asked to "reuse the node graph style of the network diagram on the
                   container info page; colors can be arbitrary and the editor needs no feature
                   labels", so this draws only a line plus socket dots, no text. The color comes
                   from the **row content** (same mapping = same color, different mappings stay
                   distinguishable) and carries no semantics (pure decoration, Accessible.ignored). */
                Canvas {
                    objectName: "wizardPortLink"
                    Layout.fillWidth: true
                    Layout.minimumWidth: Kirigami.Units.gridUnit * 2
                    Layout.preferredHeight: Kirigami.Units.gridUnit
                    Accessible.ignored: true

                    /*! Link color of this row (readable by tests to assert "one color per row"). */
                    readonly property color linkColor: Local.ChartPalette.connectionColor(
                        "editor|" + portRowItem.containerPort + "/" + portRowItem.protocol
                        + "|" + portRowItem.hostIp + ":" + portRowItem.hostPort)

                    readonly property real lineWidth: Math.max(2, Math.round(Kirigami.Units.gridUnit * 0.28))
                    readonly property real dotRadius: lineWidth * 1.15

                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()
                    Component.onCompleted: requestPaint()

                    onPaint: {
                        const ctx = getContext("2d");
                        ctx.reset();
                        const middle = height / 2;
                        const left = dotRadius;
                        const right = width - dotRadius;
                        if (right <= left) {
                            return;
                        }

                        ctx.lineWidth = lineWidth;
                        ctx.lineCap = "round";
                        ctx.strokeStyle = linkColor;
                        ctx.beginPath();
                        ctx.moveTo(left, middle);
                        ctx.lineTo(right, middle);
                        ctx.stroke();

                        // Draw both ends as "sockets": ring in the link color, center punched out to the
                        // background (same as the topology)
                        for (const x of [left, right]) {
                            ctx.fillStyle = linkColor;
                            ctx.beginPath();
                            ctx.arc(x, middle, dotRadius, 0, Math.PI * 2);
                            ctx.fill();
                            ctx.fillStyle = Kirigami.Theme.backgroundColor;
                            ctx.beginPath();
                            ctx.arc(x, middle, dotRadius * 0.42, 0, Math.PI * 2);
                            ctx.fill();
                        }
                    }
                }

                QQC2.TextField {
                    objectName: "wizardContainerPort"
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 6
                    Accessible.name: i18n("Container port")
                    horizontalAlignment: TextInput.AlignRight
                    inputMethodHints: Qt.ImhDigitsOnly
                    text: portRowItem.containerPort === 0 ? "" : String(portRowItem.containerPort)
                    validator: IntValidator {
                        bottom: 1
                        top: 65535
                    }
                    // As above: write back on focus loss/Enter only, never mid-typing
                    onEditingFinished: editor.setField(portRowItem.index,
                                                       "containerPort",
                                                       text.length === 0 ? 0 : parseInt(text, 10))
                }

                QQC2.ComboBox {
                    objectName: "wizardPortProtocol"
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 5
                    textRole: "text"
                    valueRole: "value"
                    model: [
                        {text: i18n("TCP"), value: "tcp"},
                        {text: i18n("UDP"), value: "udp"}
                    ]
                    Component.onCompleted: currentIndex = indexOfValue(portRowItem.protocol)
                    onActivated: editor.setField(portRowItem.index, "protocol", currentValue)
                }

                QQC2.Button {
                    objectName: "wizardRemovePort"
                    icon.name: "list-remove"
                    flat: true
                    Accessible.name: i18n("Remove this port")
                    onClicked: editor.remove(portRowItem.index)
                }
            }

            /*
             * Inline status: shown only when there **is** a problem (the user asked for
             * no hint while idle).
             *
             * Data comes from the controller's `portRowStatuses` (a **property**, so hints
             * update as soon as the container list changes): taken by a running container →
             * say who holds it + one-click "Use port N"; duplicate within the same request →
             * explain (suggestions avoid ports already used in this form).
             */
            Kirigami.InlineMessage {
                objectName: "wizardPortRowStatus"
                Layout.fillWidth: true
                visible: portRowItem.hasStatusError
                type: Kirigami.MessageType.Error
                text: portRowItem.hasStatusError ? root.portStatusText(portRowItem.rowStatus) : ""
            }

            /*
             * "Use port N": writes the suggested port into this row with one click.
             *
             * A standalone button rather than the InlineMessage `actions`: those hold
             * `Kirigami.Action` (a QObject, not an Item), which tests can neither find nor
             * click, while this button must be clickable by the "click the hint and it
             * disappears" test.
             */
            QQC2.Button {
                objectName: "wizardPortSuggestionButton"
                Layout.alignment: Qt.AlignRight
                visible: portRowItem.hasStatusError && root.portSuggestionText(portRowItem.rowStatus).length > 0
                text: root.portSuggestionText(portRowItem.rowStatus)
                icon.name: "edit-copy"
                onClicked: editor.setField(portRowItem.index, "hostPort", portRowItem.rowStatus.suggestion)
            }
        }
    }

    QQC2.Button {
        objectName: "wizardAddPort"
        text: i18n("Add port")
        icon.name: "list-add"
        onClicked: editor.add()
    }

    QQC2.Label {
        objectName: "wizardNoPorts"
        Layout.fillWidth: true
        visible: portRows.count === 0
        text: i18n("No published ports. The container will only be reachable on its networks.")
        font: Kirigami.Theme.smallFont
        opacity: 0.75
        wrapMode: Text.WordWrap
    }
}
