/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Empty-state placeholder (ARCH_V3 §2.1 / ARCH_V3_pre §1.3):

    "no containers", "no mounts", "logging not yet available" and similar cases all use
    Kirigami.PlaceholderMessage, never blank space or one small line of text.

    The **decision** between the four states (no data / no search match / no filter match /
    load failure) stays in the page (ARCH_V2 §33); this component only presents.

    Usage:

        Components.EmptyPlaceholder {
            Layout.fillWidth: true
            message: root.containersEmptyState().message
            explanationText: ...
            actionText: root.containersEmptyState().actionText
            onActionTriggered: root.clearContainerFilters()
        }
*/

import QtQuick

import org.kde.kirigami as Kirigami

Kirigami.PlaceholderMessage {
    id: placeholder

    /*! Main text; an empty message hides the whole placeholder. */
    required property string message

    /*! Additional explanation; hidden when empty. */
    property string explanationText: ""

    /*! Icon name; empty hides the icon (not every empty state needs a big icon). */
    property string iconName: ""

    /*! Guidance action text; empty hides the button. */
    property string actionText: ""

    /*! Guidance action icon. */
    property string actionIconName: "view-refresh"

    /*! Guidance action triggered. */
    signal actionTriggered

    // Empty text hides everything: callers need not (and should not) maintain visible themselves
    visible: placeholder.message.length > 0

    text: placeholder.message
    explanation: placeholder.explanationText
    icon.name: placeholder.iconName

    readonly property Kirigami.Action placeholderAction: Kirigami.Action {
        text: placeholder.actionText
        icon.name: placeholder.actionIconName
        enabled: placeholder.actionText.length > 0
        onTriggered: placeholder.actionTriggered()
    }

    helpfulAction: placeholder.actionText.length > 0 ? placeholder.placeholderAction : null

    Accessible.role: Accessible.StaticText
    Accessible.name: placeholder.explanationText.length > 0 ? placeholder.message + ". " + placeholder.explanationText : placeholder.message
}
