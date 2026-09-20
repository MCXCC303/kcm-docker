/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later

    String list editor (supplement to ARCH_V5_V8 §1.6).

    For fields that are "a series of strings", such as registry mirrors and
    insecure-registries: add, remove, move up/down (order matters for mirrors: earlier
    ones are tried first).

    Validation is injected through the `validator` callback (an empty string means valid,
    otherwise the error text), so "what counts as a valid mirror address" has one
    implementation only.

    Usage:

        Components.StringListEditor {
            id: mirrors
            initialEntries: ["https://mirror.example.com"]
            validator: function (value) { return root.validateMirror(value); }
            onChanged: root.markDirty()
        }
*/

// Delegates need the outer id (to add/remove/modify the ListModel): pin Unbound semantics
pragma ComponentBehavior: Unbound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

import "." as Local

ColumnLayout {
    id: root

    /*! Initial entries (read once when the component is created). */
    property var initialEntries: []
    /*!
     * Validation callback: `function(value) -> string`.
     * An empty string means valid; a returned text is shown under that row and blocks submission.
     */
    property var validator: null
    /*! Text of the add button. */
    property string addText: i18n("Add")
    /*! Placeholder text of the input. */
    property string placeholderText: ""

    /*! Entries changed (added, removed, edited or reordered). */
    signal changed

    /*! Whether the list is editable; false while the protected area is locked (fields stay visible). */
    property bool editable: true

    spacing: Kirigami.Units.smallSpacing

    ListModel {
        id: entries

        Component.onCompleted: root.syncFromInitialEntries()
    }

    /*!
     * Sync the list given by the controller into the model.
     *
     * Why "is the content identical" is checked: automatic refreshes re-evaluate
     * `initialEntries`, and an unconditional rebuild would drop the row the user is typing
     * in (and the cursor position) — with identical content the rebuild is pointless anyway:
     * edits in the control are written back to the controller immediately, so both sides
     * agreeing is exactly the "do not touch it" signal. Only real external changes (re-reading
     * the file, restoring a backup) reach the rebuild.
     */
    onInitialEntriesChanged: root.syncFromInitialEntries()

    function syncFromInitialEntries(): void {
        const incoming = [];
        for (const value of root.initialEntries) {
            incoming.push(String(value));
        }
        // Compare against the **meaningful** content of the current entries, not raw rows: after
        // "Add" the user has an empty draft row, which values() drops, so the controller's list
        // does not contain it. A raw row comparison would treat that empty row as an "external
        // change" and clear it — the symptom being "clicking Add only marks the form unsaved and
        // no entry appears" (real feedback).
        const current = root.values();
        if (incoming.length === current.length) {
            let identical = true;
            for (let i = 0; i < incoming.length; ++i) {
                if (current[i] !== incoming[i].trim()) {
                    identical = false;
                    break;
                }
            }
            if (identical) {
                return;
            }
        }
        entries.clear();
        for (const value of incoming) {
            entries.append({
                value: value
            });
        }
    }

    Repeater {
        model: entries

        delegate: RowLayout {
            required property int index
            required property string value

            objectName: "stringEntryRow"
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                QQC2.TextField {
                    id: valueField

                    objectName: "stringEntryField"
                    Layout.fillWidth: true
                    text: parent.parent.value
                    readOnly: !root.editable
                    placeholderText: root.placeholderText
                    onTextEdited: {
                        root.changed();
                        entries.setProperty(index, "value", text);
                    }
                }

                QQC2.Label {
                    objectName: "stringEntryError"
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: root.validator ? root.validator(valueField.text) : ""
                    color: Local.StatusPalette.color("negative")
                    wrapMode: Text.WordWrap
                    font: Kirigami.Theme.smallFont
                }
            }

            QQC2.ToolButton {
                objectName: "stringEntryUpButton"
                icon.name: "go-up"
                enabled: root.editable && index > 0
                Accessible.name: i18n("Move up")
                onClicked: {
                    // Order matters: emit the signal first, then change the model — remove()/move()
                    // synchronously destroy the current delegate, after which the outer id throws a
                    // ReferenceError (hit in practice)
                    root.changed();
                    entries.move(index, index - 1, 1);
                }
            }

            QQC2.ToolButton {
                objectName: "stringEntryDownButton"
                icon.name: "go-down"
                enabled: root.editable && index < entries.count - 1
                Accessible.name: i18n("Move down")
                onClicked: {
                    root.changed();
                    entries.move(index, index + 1, 1);
                }
            }

            QQC2.ToolButton {
                objectName: "stringEntryRemoveButton"
                icon.name: "list-remove"
                enabled: root.editable
                Accessible.name: i18n("Remove")
                onClicked: {
                    root.changed();
                    entries.remove(index);
                }
            }
        }
    }

    QQC2.Button {
        objectName: "stringListAddButton"
        Layout.alignment: Qt.AlignLeft
        text: root.addText
        icon.name: "list-add"
        enabled: root.editable
        onClicked: {
            entries.append({
                value: ""
            });
            root.changed();
        }
    }

    /*! Current entries (trimmed, empty rows dropped). */
    function values(): var {
        const result = [];
        for (let i = 0; i < entries.count; ++i) {
            const value = String(entries.get(i).value).trim();
            if (value.length > 0) {
                result.push(value);
            }
        }
        return result;
    }

    /*! Whether any entry failed validation (empty rows are not errors; they are ignored on submit). */
    function hasErrors(): bool {
        if (!root.validator) {
            return false;
        }
        for (let i = 0; i < entries.count; ++i) {
            const value = String(entries.get(i).value);
            if (value.trim().length === 0) {
                continue;
            }
            if (root.validator(value).length > 0) {
                return true;
            }
        }
        return false;
    }

    /*! Replace the current content with a new set of values. */
    function setValues(list): void {
        entries.clear();
        for (const value of list) {
            entries.append({
                value: String(value)
            });
        }
        root.changed();
    }
}
