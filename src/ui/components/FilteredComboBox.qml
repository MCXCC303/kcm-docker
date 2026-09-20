/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Searchable dropdown (ARCH_V5_V8 §F3).

    User feedback: "image and mount-preset selection should be dropdowns (some users have
    many image versions)". Plain `QQC2.ComboBox` has no built-in search, and custom popups
    proved unreliable in offscreen tests (hit twice in this project), so this combines a
    **search field + dropdown**:

        - search field on top: filters as you type (case-insensitive, matches `textRole`)
        - dropdown below: lists only the filtered entries; on selection `selected(entry)` hands
          the **whole entry** back to the caller
        - entries must be a **property** (not a function call): a function call creates no
          dependency and the list never updates (hit once each in logs, networks, volumes, presets)

    Usage:

        Components.FilteredComboBox {
            entries: page.controller.availableImages
            textRole: "reference"
            onSelected: function (entry) { page.controller.image = entry.reference; }
        }
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

ColumnLayout {
    id: root

    /*! Full entry list (`[{...}]`, must be a property). */
    required property var entries
    /*! Field name used for display and search matching. */
    property string textRole: "text"
    /*! Search field placeholder. */
    property string searchPlaceholder: i18n("Search…")
    /*! Placeholder while the dropdown has no selection. */
    property string placeholder: i18n("Choose…")

    /*! One entry selected (the whole entry is returned; the caller picks its fields). */
    signal selected(var entry)

    objectName: "filteredComboBox"
    spacing: Kirigami.Units.smallSpacing / 2

    /*! Filtered entries (all of them while the search is empty). */
    readonly property var filteredEntries: {
        const needle = searchField.text.trim().toLowerCase();
        if (needle.length === 0) {
            return root.entries;
        }
        const result = [];
        for (const entry of root.entries) {
            const text = String(entry[root.textRole] ?? "").toLowerCase();
            if (text.indexOf(needle) >= 0) {
                result.push(entry);
            }
        }
        return result;
    }

    QQC2.TextField {
        id: searchField

        objectName: "filteredComboBoxSearch"
        Layout.fillWidth: true
        placeholderText: root.searchPlaceholder
        Accessible.name: root.searchPlaceholder
        // Clear search: one click back to the full list
        rightPadding: Kirigami.Units.gridUnit * 2
        onTextChanged: combo.currentIndex = -1

        QQC2.ToolButton {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            visible: searchField.text.length > 0
            icon.name: "edit-clear"
            flat: true
            Accessible.name: i18n("Clear the search")
            onClicked: searchField.text = ""
        }
    }

    QQC2.ComboBox {
        id: combo

        objectName: "filteredComboBoxList"
        Layout.fillWidth: true
        model: root.filteredEntries
        textRole: root.textRole
        displayText: currentIndex >= 0 ? currentText : root.placeholder
        enabled: root.filteredEntries.length > 0
        onActivated: function (index) {
            const entry = root.filteredEntries[index];
            if (entry !== undefined) {
                root.selected(entry);
            }
        }
    }

    QQC2.Label {
        objectName: "filteredComboBoxEmpty"
        Layout.fillWidth: true
        // Hint only when the **search** matches nothing; an inherently empty list needs no note
        visible: root.filteredEntries.length === 0 && searchField.text.trim().length > 0
        text: i18n("Nothing matches the search.")
        font: Kirigami.Theme.smallFont
        opacity: 0.7
        wrapMode: Text.WordWrap
    }
}
