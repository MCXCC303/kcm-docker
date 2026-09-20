/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later

    The single presentation of operation results (ARCH_V4 §2.2.4).

    Success / already in target state / cancelled / failure are all read from the
    OperationController result channel; pages must not build their own text or
    pick their own colors:

        Components.OperationMessage {
            Layout.fillWidth: true
            operations: root.controller.operations
        }

    The category (resultCategoryKey) sets the strength: "user can fix it themselves"
    is a warning, "environment / unexpected" is an error, success and cancel are
    info/positive — not every failure becomes the same red bar.
*/

import QtQuick
import QtQuick.Controls as QQC2

import org.kde.kirigami as Kirigami
import org.kde.kcm.docker as Kontainer

Kirigami.InlineMessage {
    id: message

    required property var operations

    readonly property bool isFailure: operations.resultCategoryKey === "userActionable"
        || operations.resultCategoryKey === "environment"
        || operations.resultCategoryKey === "unexpected"

    /*! Raw engine text (untranslated) appended to the summary: it is the only "why" users can report. */
    readonly property string composedText: operations.resultDetailText.length > 0
        ? operations.resultText + "\n" + operations.resultDetailText
        : operations.resultText

    objectName: "operationMessage"

    /*!
     * Lifetime of success/cancel banners in ms; 0 = never auto-dismiss.
     *
     * Rule (user feedback A7): **success** messages have no value after a refresh or
     * page change and must not occupy the page; **failure** messages stay — they are
     * often the only "why" the user can see, and can only be closed manually.
     */
    property int autoDismissMs: 8000

    visible: operations.resultKey !== "none" && message.composedText.length > 0
    onVisibleChanged: {
        if (visible && !message.isFailure && message.autoDismissMs > 0) {
            autoDismissTimer.restart();
        } else if (!visible) {
            autoDismissTimer.stop();
        }
    }

    Timer {
        id: autoDismissTimer
        interval: message.autoDismissMs
        repeat: false
        onTriggered: message.operations.dismissResult()
    }
    type: {
        if (message.isFailure) {
            return operations.resultCategoryKey === "userActionable" ? Kirigami.MessageType.Warning : Kirigami.MessageType.Error;
        }
        return operations.resultKey === "cancelled" ? Kirigami.MessageType.Information : Kirigami.MessageType.Positive;
    }
    text: message.composedText
    showCloseButton: true

    actions: [
        Kirigami.Action {
            // For "the target is gone" errors the only useful action is refreshing the list to real
            // state (ARCH_V4 §2.2.2)
            visible: message.operations.resultActionKey === "refresh"
            text: i18n("Refresh")
            icon.name: "view-refresh"
            onTriggered: {
                message.operations.dismissResult();
                kcm.controller.refresh();
            }
        },
        Kirigami.Action {
            visible: message.isFailure
            text: i18n("Dismiss")
            icon.name: "dialog-close"
            onTriggered: message.operations.dismissResult()
        }
    ]

    Accessible.role: Accessible.StaticText
    Accessible.name: message.composedText
}
