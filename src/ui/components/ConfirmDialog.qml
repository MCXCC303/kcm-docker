/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Confirmation dialog (ARCH_V3_pre §1.3 / ARCH_V4 §2.2.5).

    Destructive operations (delete container / delete image) must go through here, with a
    **fixed sentence pattern**:

        title: <operation name>
        body:  Are you sure you want to <action> <target type> "<name>"? <consequence>

    Why not let every operation assemble its own dialog: the wording of a destructive
    operation decides whether users delete the wrong thing, so the consequence text is a
    required parameter that must not be omitted. start / stop / restart are reversible and
    get no second confirmation (ARCH_V4 §4).

    Usage:

        Components.ConfirmDialog {
            id: removeDialog
            headingText: i18n("Delete container")
            questionText: i18n("Delete the container “%1”?", page.controller.name)
            consequenceText: i18n("The container is removed; its volumes are kept.")
            acceptText: i18n("Delete")
            destructive: true
            onConfirmed: page.controller.operations.removeContainer(page.containerId)
        }
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

Kirigami.PromptDialog {
    id: dialog

    /*! Title (operation name, e.g. "Delete container"). */
    required property string headingText
    /*! The "Are you sure …?" question itself (the caller fills in the target name). */
    required property string questionText
    /*!
     * Consequence text (required): what happens when the user clicks through.
     * For example "The container is removed; its anonymous and named volumes are kept."
     */
    required property string consequenceText
    /*! Accept button text (use a verb, not "OK"). */
    property string acceptText: i18n("Continue")
    /*! Destructive operation: warning styling and a delete icon. */
    property bool destructive: false
    /*!
     * Strong confirmation: when non-empty, the user must **type this text verbatim** in the
     * dialog before confirming.
     *
     * For switches with no way back once enabled (e.g. `--privileged`, equivalent to host
     * root). A container name beats an "I understand the risk" checkbox: typing costs more
     * and makes the user look again at which object they are about to touch.
     */
    property string requireText: ""
    /*! Placeholder of the strong-confirmation field. */
    property string requireTextHint: i18n("Type “%1” to confirm", dialog.requireText)
    /*! Whether the input matches (always true while requireText is empty). */
    readonly property bool requireTextSatisfied: dialog.requireText.length === 0
        || confirmField.text.trim() === dialog.requireText.trim()

    signal confirmed

    title: dialog.headingText
    subtitle: dialog.consequenceText.length > 0 ? dialog.questionText + " " + dialog.consequenceText : dialog.questionText
    dialogType: dialog.destructive ? Kirigami.PromptDialog.Warning : Kirigami.PromptDialog.Information

    // customFooterActions instead of standardButtons: the button must name the action
    // ("Delete", not "OK"), which is itself part of preventing mistakes.
    standardButtons: Kirigami.Dialog.NoButton

    // Strong-confirmation field (shown only while requireText is non-empty).
    // Kirigami.Dialog puts default children into contentData, so declaring it here is enough.
    QQC2.TextField {
        id: confirmField

        objectName: "confirmDialogTextField"
        Layout.fillWidth: true
        visible: dialog.requireText.length > 0
        placeholderText: dialog.requireText.length > 0 ? dialog.requireTextHint : ""
        Accessible.name: dialog.requireTextHint
        onAccepted: {
            if (dialog.requireTextSatisfied) {
                dialog.close();
                dialog.confirmed();
            }
        }
    }

    onOpened: confirmField.text = ""

    customFooterActions: [
        Kirigami.Action {
            objectName: "confirmDialogAcceptAction"
            text: dialog.acceptText
            icon.name: dialog.destructive ? "edit-delete" : "dialog-ok"
            // The accept button stays disabled until the strong-confirmation text matches
            enabled: dialog.requireTextSatisfied
            onTriggered: {
                dialog.close();
                dialog.confirmed();
            }
        },
        Kirigami.Action {
            text: i18n("Cancel")
            icon.name: "dialog-cancel"
            onTriggered: dialog.close()
        }
    ]

    // Note: Accessible is an **attached property** and only attaches to Item / Action.
    // Attaching it to Kirigami.PromptDialog (not an Item) fails at runtime with
    // "Accessible attached property must be attached to an object deriving from Item or Action",
    // and the dialog already exposes title/body as accessible text anyway.
}
