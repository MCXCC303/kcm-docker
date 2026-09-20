/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Registry login dialog (ARCH_V5_V8 §2.7).

    Three hard rules (stated here so changes do not break them):

      1. **The password is never echoed or copied**: `echoMode: Password` (Qt also disables
         copying), and there is no CopyButton here — credentials only leave via "Log in".
      2. **Credentials reach the controller only after "Log in" is clicked**; the controller
         does the `POST /auth` check and only writes the wallet on success. The dialog stores
         nothing itself and discards everything on close.
      3. **Errors are shown in place**: the reason a check failed must appear in the dialog
         (the user is looking at it), not only on the page behind it.
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

Kirigami.Dialog {
    id: dialog

    /*! Default registry (Docker Hub's historical spelling, matching config.json). */
    readonly property string defaultServerAddress: "https://index.docker.io/v1/"

    /*! Pre-filled registry address (e.g. carried over from the image reference of a failed pull). */
    property string presetServerAddress: ""

    /*! Text of the last failed check (filled in by the caller after mapping the controller key). */
    property string errorText: ""
    /*! A check is running: buttons are disabled to prevent duplicate submissions. */
    property bool busy: false

    signal loginRequested(string serverAddress, string username, string password, string token)
    signal dismissed

    objectName: "registryLoginDialog"

    title: i18n("Log in to a registry")
    standardButtons: Kirigami.Dialog.NoButton
    preferredWidth: Kirigami.Units.gridUnit * 24

    /*! Reset before opening: never reuse the previous input (especially the password). */
    function reset(startAddress: string): void {
        serverField.text = startAddress.length > 0 ? startAddress : defaultServerAddress;
        userField.text = "";
        passwordField.text = "";
        tokenField.text = "";
        tokenMode.checked = false;
        errorText = "";
    }

    function submit(): void {
        if (busy) {
            return;
        }
        loginRequested(serverField.text,
                       tokenMode.checked ? "" : userField.text,
                       tokenMode.checked ? "" : passwordField.text,
                       tokenMode.checked ? tokenField.text : "");
    }

    onClosed: dialog.dismissed()

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        Kirigami.InlineMessage {
            objectName: "loginError"
            Layout.fillWidth: true
            visible: dialog.errorText.length > 0
            type: Kirigami.MessageType.Error
            text: dialog.errorText
        }

        QQC2.TextField {
            id: serverField

            objectName: "loginServerField"
            Layout.fillWidth: true
            enabled: !dialog.busy
            placeholderText: "https://index.docker.io/v1/" // i18n-lint: allow Sample address (data)
            Accessible.name: i18n("Registry")
        }

        QQC2.CheckBox {
            id: tokenMode

            objectName: "loginTokenMode"
            enabled: !dialog.busy
            text: i18n("Use an access token instead of a password")
        }

        QQC2.TextField {
            id: userField

            objectName: "loginUserField"
            Layout.fillWidth: true
            visible: !tokenMode.checked
            enabled: !dialog.busy
            Accessible.name: i18n("User name")
            placeholderText: i18n("User name")
        }

        QQC2.TextField {
            id: passwordField

            objectName: "loginPasswordField"
            Layout.fillWidth: true
            visible: !tokenMode.checked
            enabled: !dialog.busy
            // Never echoed, and no way to copy it out (Qt disables copying in password mode)
            echoMode: TextInput.Password
            Accessible.name: i18n("Password")
            placeholderText: i18n("Password")
            onAccepted: dialog.submit()
        }

        QQC2.TextField {
            id: tokenField

            objectName: "loginTokenField"
            Layout.fillWidth: true
            visible: tokenMode.checked
            enabled: !dialog.busy
            echoMode: TextInput.Password
            Accessible.name: i18n("Access token")
            placeholderText: i18n("Access token")
            onAccepted: dialog.submit()
        }
    }

    customFooterActions: [
        Kirigami.Action {
            objectName: "loginSubmitButton"
            text: dialog.busy ? i18n("Checking…") : i18n("Log in")
            icon.name: "dialog-password"
            enabled: !dialog.busy
            onTriggered: dialog.submit()
        }
    ]
}
