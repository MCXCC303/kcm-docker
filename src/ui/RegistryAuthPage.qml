/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Registry credentials page (ARCH_V5_V8 §2.6/§2.7).

    Three jobs only, none of which touches credential contents:

      1. **State the situation**: whether the wallet works and which registries are stored
         (address + user name, never passwords/tokens);
      2. **Log in and test**: login goes through `RegistryLoginDialog`; the controller validates
         with `POST /auth` first and only then writes the wallet. "Test" re-checks stored credentials;
      3. **One-off import from the docker CLI**: read-only scan of `~/.docker/config.json`, listing
         registries not yet in the wallet for the user to select (existing entries are not overwritten).

    All wording comes from the key → text mapping here (C++ only emits stable keys).
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kcmutils as KCM
import org.kde.kirigami as Kirigami

import "components" as Components

KCM.AbstractKCM {
    id: page

    readonly property var auth: kcm.controller.registryAuth
    /*! Pre-filled registry address (passed in from the "Log in…" hint; empty means use the default). */
    property string presetServerAddress: ""
    readonly property real contentMaxWidth: Kirigami.Units.gridUnit * 42
    /*! Address of the registry awaiting "Remove" confirmation. */
    property string pendingRemovalAddress: ""

    signal closeRequested

    objectName: "registryAuthPage"

    Component.onCompleted: {
        page.auth.refresh();
        if (page.presetServerAddress.length > 0 && page.auth.walletStateKey === "ready") {
            page.openLoginDialog(page.presetServerAddress);
        }
    }

    /*! Result / error key → user-facing text (C++ emits only keys, the text lives here). */
    function messageFor(key: string): string {
        switch (key) {
        case "loginSucceeded":
            return i18n("Logged in. The credentials are stored in KWallet.");
        case "testSucceeded":
            return i18n("The connection was verified with the stored credentials.");
        case "removed":
            return i18n("The credentials were removed.");
        case "invalidCredentials":
            return i18n("The registry rejected these credentials (user name, password or token).");
        case "registryUnreachable":
            return i18n("The registry could not be reached: check the network, DNS or a proxy.");
        case "failed":
            return i18n("The registry check failed.");
        case "noCredentialStored":
            return i18n("No credentials are stored for this registry yet.");
        case "unavailable":
            return i18n("KWallet is not available, so credentials cannot be read or saved.");
        case "invalidServerAddress":
            return i18n("Enter a registry address.");
        case "noCredentials":
            return i18n("Enter a user name and password, or switch to token login.");
        case "writeFailed":
            return i18n("The credentials could not be written to KWallet.");
        case "removeFailed":
            return i18n("The credentials could not be removed.");
        case "cliWriteFailed":
            return i18n("The credentials are stored, but writing them to the Docker CLI configuration (%1) failed. The Docker CLI will not see them.",
                        page.auth.lastErrorDetail);
        default:
            return "";
        }
    }

    /*! Wallet-unavailable reason key → explanation. */
    function walletReasonText(): string {
        switch (page.auth.walletUnavailableReason) {
        case "walletDisabled":
            return i18n("KWallet is disabled in the system settings. Kontainer does not fall back to plain-text storage.");
        case "walletFolderFailed":
            return i18n("KWallet is open, but the Kontainer folder could not be created.");
        default:
            return i18n("KWallet could not be opened (the unlock request may have been cancelled).");
        }
    }

    /*! Open the login dialog (optionally with a pre-filled registry). */
    function openLoginDialog(serverAddress: string): void {
        loginDialog.reset(serverAddress);
        page.auth.clearResult();
        loginDialog.open();
    }

    Connections {
        target: page.auth

        function onResultChanged() {
            // Close the dialog on success (on failure keep the reason visible inside it)
            if (page.auth.lastResultKey === "loginSucceeded") {
                loginDialog.close();
            } else if (loginDialog.opened) {
                loginDialog.errorText = page.messageFor(page.auth.lastErrorKey);
            }
        }
    }

    actions: [
        Kirigami.Action {
            text: i18n("Log in…")
            icon.name: "dialog-password"
            enabled: page.auth.walletStateKey === "ready"
            onTriggered: page.openLoginDialog()
        },
        Kirigami.Action {
            text: i18n("Back")
            icon.name: "go-previous"
            onTriggered: page.closeRequested()
        }
    ]

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        QQC2.ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: Math.min(parent.width, page.contentMaxWidth)
                x: Math.max(0, (parent.width - width) / 2)
                spacing: Kirigami.Units.largeSpacing

                Kirigami.Heading {
                    Layout.fillWidth: true
                    Layout.topMargin: Kirigami.Units.smallSpacing
                    level: 2
                    text: i18n("Registry credentials")
                }

                /* ---------------- Result / error ---------------- */
                Kirigami.InlineMessage {
                    objectName: "authResultMessage"
                    Layout.fillWidth: true
                    visible: page.auth.lastResultKey.length > 0
                    type: Kirigami.MessageType.Positive
                    text: page.messageFor(page.auth.lastResultKey)
                    showCloseButton: true
                    onVisibleChanged: {
                        if (!visible) {
                            page.auth.clearResult();
                        }
                    }
                }

                Kirigami.InlineMessage {
                    objectName: "authErrorMessage"
                    Layout.fillWidth: true
                    visible: page.auth.lastErrorKey.length > 0
                    type: Kirigami.MessageType.Error
                    text: page.messageFor(page.auth.lastErrorKey)
                    showCloseButton: true
                    onVisibleChanged: {
                        if (!visible) {
                            page.auth.clearResult();
                        }
                    }
                }

                /* ---------------- Wallet state ---------------- */
                Kirigami.InlineMessage {
                    objectName: "walletBanner"
                    Layout.fillWidth: true
                    visible: page.auth.walletStateKey !== "ready"
                    type: page.auth.walletStateKey === "unavailable" ? Kirigami.MessageType.Error : Kirigami.MessageType.Information
                    text: page.auth.walletStateKey === "opening"
                        ? i18n("Waiting for KWallet to be unlocked…")
                        : page.walletReasonText()
                    // Actions array is inlined (QML rejects a ternary with an object-array literal);
                    // Action.visible decides whether it is needed
                    actions: [
                        Kirigami.Action {
                            text: i18n("Try again")
                            icon.name: "view-refresh"
                            visible: page.auth.walletStateKey === "unavailable"
                            onTriggered: page.auth.refresh()
                        }
                    ]
                }

                /* ---------------- Stored registries ---------------- */
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Heading {
                        Layout.fillWidth: true
                        level: 3
                        text: i18n("Stored credentials")
                    }

                    // Prominent second entry for adding credentials (testing: toolbar one was hard to find)
                    QQC2.Button {
                        objectName: "addCredentialButton"
                        text: i18n("Add credential…")
                        icon.name: "list-add"
                        enabled: page.auth.walletStateKey === "ready"
                        onClicked: page.openLoginDialog(page.auth.credentials.empty ? page.presetServerAddress : "")
                    }
                }

                // Entries owned by an external credential helper: we **never** call them, but must
                // say so, otherwise users wonder why a registry they logged into via the CLI is missing
                QQC2.Label {
                    objectName: "helperManagedHint"
                    Layout.fillWidth: true
                    visible: page.auth.helperManagedKeys.length > 0
                    text: i18n("These registries are managed by an external credential helper and were not taken over: %1",
                              page.auth.helperManagedKeys.join(", "))
                    font: Kirigami.Theme.smallFont
                    opacity: 0.8
                    wrapMode: Text.WordWrap
                }

                Components.EmptyPlaceholder {
                    objectName: "credentialsEmptyPlaceholder"
                    Layout.fillWidth: true
                    message: page.auth.credentials.empty ? i18n("No credentials stored.") : ""
                }

                Repeater {
                    model: page.auth.credentials

                    delegate: RowLayout {
                        id: credentialRow

                        required property int index
                        required property string serverAddress
                        required property string authKind
                        required property string username

                        objectName: "credentialRow"
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        Components.FieldChip {
                            objectName: "credentialAddressChip"
                            text: credentialRow.serverAddress
                        }

                        Components.FieldChip {
                            objectName: "credentialKindChip"
                            muted: true
                            text: credentialRow.authKind === "token" ? i18n("token") : credentialRow.username
                        }

                        Item {
                            Layout.fillWidth: true
                        }

                        QQC2.Button {
                            objectName: "credentialTestButton"
                            text: i18n("Test")
                            icon.name: "network-connect"
                            enabled: page.auth.walletStateKey === "ready" && !page.auth.busy
                            onClicked: page.auth.testCredential(credentialRow.serverAddress)
                        }

                        QQC2.Button {
                            objectName: "credentialRemoveButton"
                            text: i18n("Remove")
                            icon.name: "edit-delete"
                            enabled: page.auth.walletStateKey === "ready"
                            onClicked: {
                                page.pendingRemovalAddress = credentialRow.serverAddress;
                                removeCredentialDialog.open();
                            }
                        }
                    }
                }

                Item {
                    Layout.fillHeight: true
                }
            }
        }
    }

    Components.RegistryLoginDialog {
        id: loginDialog

        busy: page.auth.busy
        onLoginRequested: function (serverAddress, username, password, token) {
            page.auth.login(serverAddress, username, password, token);
        }
        onDismissed: page.auth.clearResult()
    }

    Components.ConfirmDialog {
        id: removeCredentialDialog

        objectName: "removeCredentialDialog"
        headingText: i18n("Remove credentials")
        questionText: i18n("Remove the stored credentials for “%1”?", page.pendingRemovalAddress)
        consequenceText: i18n("Pulling private images from this registry will need a new login.")
        acceptText: i18n("Remove")
        destructive: true
        onConfirmed: page.auth.removeCredential(page.pendingRemovalAddress)
    }
}
