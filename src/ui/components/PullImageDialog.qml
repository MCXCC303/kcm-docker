/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Pull image dialog (ARCH_V4 §2.4).

    This dialog **only starts** a pull: it closes as soon as "Pull" is pressed, pulling
    continues in the background, and both progress and result show up in the image tab's
    pull list (`PullProgressList`).

    Why progress is not shown here: a pull can take minutes and users have no reason to be
    pinned down by a modal window; closing the dialog must not interrupt the pull either.
    A repeated pull of the same reference is rejected with an explanation; different images
    can pull concurrently.

    Also: do not write `button.trigger()` — `QQC2.Button` has no such method (that is
    Action's), so it only throws a TypeError on Enter at runtime (and does nothing).
    Enter and the button share one function.
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

import "." as Local

Kirigami.Dialog {
    id: dialog

    required property var operations

    /*!
        Reference validation and hints are delegated to ImageRefInput, which calls the single
        C++ implementation (`isValidImageReference` / `normalizedImageReference`); no duplicate here.
    */
    readonly property bool referenceValid: referenceInput.referenceValid
    readonly property string normalizedReference: referenceInput.normalizedReference
    readonly property bool alreadyPulling: referenceInput.alreadyPulling

    signal pullRequested(string reference)
    /*! "Log in first…": brings the registry of this image to the authentication page (ARCH_V5_V8 §2.7). */
    signal loginRequested(string serverAddress)

    /*! Whether this image's registry already has credentials (passed in from RegistryAuthController). */
    property bool credentialKnown: true

    /*! Input control (the caller reads the current reference and registry address from it). */
    property alias referenceInput: referenceInput

    objectName: "pullImageDialog"

    title: i18n("Pull image")
    preferredWidth: Kirigami.Units.gridUnit * 26
    padding: Kirigami.Units.largeSpacing

    function reset() {
        referenceInput.text = "";
        referenceInput.forceActiveFocus();
    }

    /*! Start the pull (Enter and the "Pull" button share this one path). */
    function startPull() {
        if (!dialog.referenceValid || dialog.alreadyPulling) {
            return;
        }
        const reference = dialog.normalizedReference;
        dialog.close();
        dialog.pullRequested(reference);
    }

    ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            Layout.fillWidth: true
            text: i18n("Image reference (for example “alpine:3.19” or “registry.example.com/team/app”).")
            wrapMode: Text.WordWrap
            font: Kirigami.Theme.smallFont
            opacity: 0.8
        }

        Local.ImageRefInput {
            id: referenceInput

            Layout.fillWidth: true
            operations: dialog.operations
            placeholderText: i18n("alpine:3.19")
            onAccepted: dialog.startPull()
        }

        // Not logged in to this registry: say up front that the pull will fail, and offer a way to log in
        Kirigami.InlineMessage {
            objectName: "pullNeedsLoginHint"
            Layout.fillWidth: true
            visible: dialog.referenceValid && !dialog.credentialKnown
            type: Kirigami.MessageType.Information
            text: i18n("No credentials are stored for this registry. A private image will fail to pull until you log in.")
            actions: [
                Kirigami.Action {
                    text: i18n("Log in…")
                    icon.name: "dialog-password"
                    onTriggered: {
                        dialog.close();
                        dialog.loginRequested(dialog.referenceInput.serverAddress);
                    }
                }
            ]
        }

    }

    footer: QQC2.DialogButtonBox {
        QQC2.Button {
            id: pullButton

            objectName: "pullImageButton"
            text: i18n("Pull")
            icon.name: "download"
            enabled: dialog.referenceValid && !dialog.alreadyPulling
            QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.AcceptRole
            onClicked: dialog.startPull()
        }

        QQC2.Button {
            objectName: "pullDialogCloseButton"
            text: i18n("Close")
            icon.name: "dialog-close"
            QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.RejectRole
            onClicked: dialog.close()
        }
    }
}
