/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Key/value editor (ARCH_V5_V8 §1.6).

    Used for environment variables, container/network/volume labels and Dockerfile build
    arguments, so "two inputs per row + add/remove" is not rewritten every time.

    Three things it must own centrally:

      - **key validation has one implementation**: calls C++ `Presentation.isValidEnvKey()`,
        QML holds no second regex
      - **values can be masked** (`secretValues`): environment variables often hold
        secrets, so they are not echoed by default
      - **pasting `.env` text**: parsing also goes through C++
        (`Presentation.parseEnvText()`), supporting comments, blank lines, the `export `
        prefix and quoting

    Usage:

        Components.KeyValueListEditor {
            id: envEditor
            initialEntries: [{ key: "TZ", value: "Asia/Shanghai" }]
            keyPlaceholderText: "TZ"
            secretValues: true
            onChanged: root.markDirty()
        }
*/

// Delegates need the outer id (to add/remove/modify the ListModel): keep the Unbound semantics
pragma ComponentBehavior: Unbound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.kcm.docker as Kontainer

import "." as Local

ColumnLayout {
    id: root

    /*! Initial entries: `[{ key, value }]` (read once when the component is created). */
    property var initialEntries: []
    /*! Key placeholder (for example `TZ`). */
    property string keyPlaceholderText: "KEY"
    /*! Value placeholder. */
    property string valuePlaceholderText: "value"
    /*! Values are masked by default (environment variables may be secrets). */
    property bool secretValues: false
    /*! Whether to show the "Paste .env" button. */
    property bool envPasteEnabled: false

    /*! Entries changed. */
    signal changed

    /*!
     * Relay object for delegates.
     *
     * Under `pragma ComponentBehavior: Unbound`, referencing the **root object's id**
     * (root) from a delegate throws `ReferenceError: root is not defined` — the error seen
     * in user testing. Ids of **non-root** objects in the same file do work, so everything
     * delegates need is collected here and forwarded to the root. This avoids switching to
     * Bound (that pragma was added to fix a crash and must not be dropped lightly).
     */
    QtObject {
        id: editor

        /* Read-only config for delegates: also via here, reading root.* in a delegate throws too */
        readonly property string keyPlaceholder: root.keyPlaceholderText
        readonly property string valuePlaceholder: root.valuePlaceholderText
        readonly property bool secretValues: root.secretValues
        readonly property var model: rows
        readonly property var presentation: Kontainer.Presentation

        /*! Remove a row (called by delegates). */
        function removeRow(row) {
            rows.remove(row);
            root.changed();
        }
        /*! Key/value edited (called by delegates). */
        function setEntry(row, key, value) {
            if (key !== undefined) {
                rows.setProperty(row, "entryKey", key);
            }
            if (value !== undefined) {
                rows.setProperty(row, "entryValue", value);
            }
            root.changed();
        }
    }

    spacing: Kirigami.Units.smallSpacing

    ListModel {
        id: rows

        Component.onCompleted: {
            for (const entry of root.initialEntries) {
                rows.append({
                    entryKey: String(entry.key ?? ""),
                    entryValue: String(entry.value ?? "")
                });
            }
        }
    }

    Repeater {
        model: rows

        delegate: ColumnLayout {
            required property int index
            required property string entryKey
            required property string entryValue

            objectName: "keyValueRow"
            Layout.fillWidth: true
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                QQC2.TextField {
                    id: keyField

                    objectName: "keyValueKeyField"
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 10
                    text: entryKey
                    placeholderText: editor.keyPlaceholder
                    onTextEdited: editor.setEntry(index, text, undefined)
                }

                QQC2.TextField {
                    id: valueField

                    objectName: "keyValueValueField"
                    Layout.fillWidth: true
                    text: entryValue
                    placeholderText: editor.valuePlaceholder
                    echoMode: revealSecret.checked || !editor.secretValues ? TextInput.Normal : TextInput.Password
                    onTextEdited: editor.setEntry(index, undefined, text)
                }

                // A reveal toggle is only needed where the value may be a secret
                QQC2.ToolButton {
                    objectName: "keyValueRevealButton"
                    visible: editor.secretValues
                    checkable: true
                    id: revealSecret
                    icon.name: checked ? "password-show-off" : "password-show-on"
                    Accessible.name: i18n("Show value")
                    QQC2.ToolTip.text: i18n("Show value")
                    QQC2.ToolTip.visible: hovered
                }

                QQC2.ToolButton {
                    objectName: "keyValueRemoveButton"
                    icon.name: "list-remove"
                    Accessible.name: i18n("Remove")
                    onClicked: editor.removeRow(index)
                }
            }

            QQC2.Label {
                objectName: "keyValueKeyError"
                Layout.fillWidth: true
                visible: text.length > 0
                text: keyField.text.length === 0 || editor.presentation.isValidEnvKey(keyField.text)
                    ? ""
                    : i18n("Invalid name: use letters, digits and underscore, and do not start with a digit.")
                color: Local.StatusPalette.color("negative")
                wrapMode: Text.WordWrap
                font: Kirigami.Theme.smallFont
            }

            QQC2.Label {
                objectName: "keyValueDuplicateError"
                Layout.fillWidth: true
                visible: text.length > 0
                text: {
                    if (keyField.text.length === 0) {
                        return "";
                    }
                    for (let i = 0; i < editor.model.count; ++i) {
                        if (i !== index && String(editor.model.get(i).entryKey) === keyField.text) {
                            return i18n("Duplicate name.");
                        }
                    }
                    return "";
                }
                color: Local.StatusPalette.color("negative")
                wrapMode: Text.WordWrap
                font: Kirigami.Theme.smallFont
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.Button {
            objectName: "keyValueAddButton"
            text: i18n("Add")
            icon.name: "list-add"
            onClicked: {
                rows.append({
                    entryKey: "",
                    entryValue: ""
                });
                root.changed();
            }
        }

        QQC2.Button {
            objectName: "keyValuePasteEnvButton"
            visible: root.envPasteEnabled
            text: i18n("Paste .env…")
            icon.name: "edit-paste"
            onClicked: envPasteDialog.open()
        }

        Item {
            Layout.fillWidth: true
        }
    }

    /*! Current entries (fully empty rows are dropped). */
    function entries(): var {
        const result = [];
        for (let i = 0; i < rows.count; ++i) {
            const row = rows.get(i);
            const key = String(row.entryKey).trim();
            if (key.length === 0) {
                continue;
            }
            result.push({
                key: key,
                value: String(row.entryValue)
            });
        }
        return result;
    }

    /*! Whether a key is invalid or duplicated (blank rows are no error, they are ignored on submit). */
    function hasErrors(): bool {
        const seen = [];
        for (let i = 0; i < rows.count; ++i) {
            const key = String(rows.get(i).entryKey).trim();
            if (key.length === 0) {
                continue;
            }
            if (!Kontainer.Presentation.isValidEnvKey(key)) {
                return true;
            }
            if (seen.indexOf(key) >= 0) {
                return true;
            }
            seen.push(key);
        }
        return false;
    }

    /*! Replace the current content with a new set of entries. */
    function setEntries(list): void {
        rows.clear();
        for (const entry of list) {
            rows.append({
                entryKey: String(entry.key ?? ""),
                entryValue: String(entry.value ?? "")
            });
        }
        root.changed();
    }

    /*! Append entries (used by .env paste); keys that already exist are overwritten. */
    function appendEntries(list): void {
        for (const entry of list) {
            let replaced = false;
            for (let i = 0; i < rows.count; ++i) {
                if (String(rows.get(i).entryKey).trim() === entry.key) {
                    rows.setProperty(i, "entryValue", entry.value);
                    replaced = true;
                    break;
                }
            }
            if (!replaced) {
                rows.append({
                    entryKey: entry.key,
                    entryValue: entry.value
                });
            }
        }
        root.changed();
    }

    /*! Input for pasted `.env` text (multiline, merged on accept). */
    Kirigami.PromptDialog {
        id: envPasteDialog

        objectName: "envPasteDialog"
        title: i18n("Paste .env content")
        dialogType: Kirigami.PromptDialog.Information
        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
        onAccepted: root.appendEntries(Kontainer.Presentation.parseEnvText(envPasteField.text))

        QQC2.ScrollView {
            Layout.preferredWidth: Kirigami.Units.gridUnit * 24
            Layout.preferredHeight: Kirigami.Units.gridUnit * 10

            QQC2.TextArea {
                id: envPasteField

                objectName: "envPasteField"
                // The example text is data, not UI copy
                placeholderText: "TZ=Asia/Shanghai\n# comment\nAPI_KEY=\"secret\"" // i18n-lint: allow sample .env content
                font.family: "monospace"
                wrapMode: TextEdit.NoWrap
            }
        }
    }
}
