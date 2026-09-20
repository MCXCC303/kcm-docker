/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Mount preset management (ARCH_V5_V8 §4.2).

    User feedback: "preset management deserves its own tab; managing presets inside the create
    container flow is inconvenient" — so the management UI moved here (the main page's "Mount
    presets" tab), and the wizard keeps only "quick add".

    Each row can edit the host/container paths, toggle the favourite flag, move up/down and
    delete; a row at the bottom adds new ones. Edits are written straight back to storage
    (saved on focus loss), because presets are "this tool's data" rather than a form awaiting
    submission.
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

import "." as Local

ColumnLayout {
    id: root

    /*! Preset store (`kcm.controller.mountPresets`). */
    required property var store
    /*! Directory chooser (`kcm.controller.directoryPicker`): used to pick host paths. */
    required property var directoryPicker

    /*!
     * Intermediary for delegates (under Unbound a delegate cannot reach the root id).
     */
    QtObject {
        id: manager

        readonly property var store: root.store

        function remove(presetId) {
            manager.store.remove(presetId);
        }
        function toggleFavorite(presetId, favorite) {
            manager.store.setFavorite(presetId, favorite);
        }
        function moveUp(presetId) {
            manager.store.moveUp(presetId);
        }
        function moveDown(presetId) {
            manager.store.moveDown(presetId);
        }
        function update(presetId, source, destination, readOnly, note) {
            manager.store.update(presetId, source, destination, readOnly, note);
        }
        function add(source, destination) {
            // Read-only is chosen at mount time, so presets are stored writable (field kept for old configs)
            return manager.store.add(source, destination, "bind", false, "");
        }
    }

    objectName: "mountPresetManager"
    spacing: Kirigami.Units.smallSpacing

    EmptyPlaceholder {
        objectName: "presetManagerEmpty"
        Layout.fillWidth: true
        message: root.store.empty
            ? i18n("No presets yet. Add one below, or save a mount as a preset from a container's details.")
            : ""
    }

    /* ------------------------------ New entry (on top) ------------------------------ */
    /* User report: creation belongs at the **top**, the browse button goes **before** the host
       path/volume name, and adding appends one entry to the list */
    RowLayout {
        objectName: "presetManagerNewRow"
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.Button {
            objectName: "presetManagerNewBrowse"
            icon.name: "folder-open"
            text: i18n("Browse…")
            onClicked: {
                const chosen = root.directoryPicker.chooseDirectory(newSource.text);
                if (chosen.length > 0) {
                    // On cancel (empty string) keep the current value: do not clear a hand-typed path
                    newSource.text = chosen;
                }
            }
        }

        QQC2.TextField {
            id: newSource

            objectName: "presetManagerNewSource"
            Layout.fillWidth: true
            placeholderText: i18n("Host path or volume name")
            Accessible.name: i18n("New preset source")
        }

        QQC2.Label {
            text: "→"
            opacity: 0.6
        }

        QQC2.TextField {
            id: newDestination

            objectName: "presetManagerNewDestination"
            Layout.fillWidth: true
            placeholderText: i18n("Container path")
            Accessible.name: i18n("New preset destination")
        }

        QQC2.Button {
            objectName: "presetManagerAdd"
            text: i18n("Add")
            icon.name: "list-add"
            enabled: newSource.text.trim().length > 0 && newDestination.text.trim().length > 0
            onClicked: {
                const created = manager.add(newSource.text, newDestination.text);
                if (created.length > 0) {
                    newSource.text = "";
                    newDestination.text = "";
                }
            }
        }
    }

    Repeater {
        // Must be a **property** (summaries has NOTIFY): a function call establishes no dependency,
        // so without this the list would not repopulate when presets are added or removed
        model: root.store.summaries

        delegate: Kirigami.AbstractCard {
            id: presetCard

            required property string id
            required property string source
            required property string destination
            required property string type
            required property bool readOnly
            required property string note
            required property bool favorite

            objectName: "presetManagerRow"
            Layout.fillWidth: true
            showClickFeedback: false

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing / 2

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.TextField {
                        objectName: "presetManagerSource"
                        Layout.fillWidth: true
                        placeholderText: i18n("Host path or volume name")
                        Accessible.name: i18n("Preset source")
                        text: presetCard.source
                        onEditingFinished: manager.update(presetCard.id, text, presetCard.destination, presetCard.readOnly, presetCard.note)
                    }

                    QQC2.Label {
                        text: "→"
                        opacity: 0.6
                    }

                    QQC2.TextField {
                        objectName: "presetManagerDestination"
                        Layout.fillWidth: true
                        placeholderText: i18n("Container path")
                        Accessible.name: i18n("Preset destination")
                        text: presetCard.destination
                        onEditingFinished: manager.update(presetCard.id, presetCard.source, text, presetCard.readOnly, presetCard.note)
                    }

                    // Whether a mount is read-only is decided **at mount time** (the mount rows on the
                    // create container page), not here: the same preset may be mounted read-only in one
                    // container and writable in another (user report)
                    QQC2.CheckBox {
                        objectName: "presetManagerFavorite"
                        text: i18n("Favourite")
                        checked: presetCard.favorite
                        onToggled: manager.toggleFavorite(presetCard.id, checked)
                    }

                    QQC2.ToolButton {
                        objectName: "presetManagerMoveUp"
                        icon.name: "go-up"
                        Accessible.name: i18n("Move up")
                        onClicked: manager.moveUp(presetCard.id)
                    }

                    QQC2.ToolButton {
                        objectName: "presetManagerMoveDown"
                        icon.name: "go-down"
                        Accessible.name: i18n("Move down")
                        onClicked: manager.moveDown(presetCard.id)
                    }

                    QQC2.ToolButton {
                        objectName: "presetManagerRemove"
                        icon.name: "edit-delete"
                        Accessible.name: i18n("Remove this preset")
                        onClicked: manager.remove(presetCard.id)
                    }
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    visible: presetCard.type === "volume"
                    text: i18n("Named volume")
                    font: Kirigami.Theme.smallFont
                    opacity: 0.7
                }
            }

            Accessible.name: i18n("%1 to %2", presetCard.source, presetCard.destination)
        }
    }

    // Validation rules share one implementation with the store: illegal input is rejected, with a reason
    Kirigami.InlineMessage {
        objectName: "presetManagerError"
        Layout.fillWidth: true
        visible: message.length > 0
        type: Kirigami.MessageType.Error
        property string message: {
            if (newSource.text.trim().length === 0 && newDestination.text.trim().length === 0) {
                return "";
            }
            const sourceError = root.store.sourceError(newSource.text, "bind");
            if (sourceError === "sourceNotAbsolute") {
                return i18n("Bind mounts need an absolute host path (use a volume name for a named volume).");
            }
            const destinationError = root.store.destinationError(newDestination.text);
            if (destinationError === "destinationNotAbsolute") {
                return i18n("The container path must be absolute.");
            }
            return "";
        }
    }
}
