/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    仓库认证页（ARCH_V5_V8 §2.6/§2.7）。

    这一页只做三件事，且都不碰凭据内容本身：

      1. **说明现状**：钱包是否可用、存了哪些仓库（地址 + 用户名，绝不含密码/令牌）；
      2. **登录与测试**：登录走 `RegistryLoginDialog`，由控制器先 `POST /auth` 校验、
         成功才写钱包；"测试连接"用已保存的凭据再校验一次；
      3. **从 docker CLI 一次性导入**：只读扫描 `~/.docker/config.json`，
         列出钱包里还没有的仓库，用户勾选后导入（已有条目不会被覆盖）。

    文案全部由这里的 key → 文本映射负责（C++ 只给稳定 key）。
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
    /*! 预填的仓库地址（从"去登录…"引导带过来；空则用默认值）。 */
    property string presetServerAddress: ""
    readonly property real contentMaxWidth: Kirigami.Units.gridUnit * 42
    /*! 导入列表里勾选的仓库（地址 → true）。 */
    property var importSelection: ({})
    /*! 正在等待"移除"确认的仓库地址。 */
    property string pendingRemovalAddress: ""

    signal closeRequested

    objectName: "registryAuthPage"

    Component.onCompleted: {
        page.auth.refresh();
        if (page.presetServerAddress.length > 0 && page.auth.walletStateKey === "ready") {
            page.openLoginDialog(page.presetServerAddress);
        }
    }

    onImportSelectionChanged: importCount = Object.keys(importSelection).length
    property int importCount: 0

    /*! 结果 / 错误 key → 用户文案（C++ 只给 key，文案在这里）。 */
    function messageFor(key: string): string {
        switch (key) {
        case "loginSucceeded":
            return i18n("Logged in. The credentials are stored in KWallet.");
        case "testSucceeded":
            return i18n("The connection was verified with the stored credentials.");
        case "removed":
            return i18n("The credentials were removed.");
        case "importSucceeded":
            return i18n("Imported %1 credentials from the Docker CLI configuration.", page.auth.lastImportedCount);
        case "importNothingToDo":
            return i18n("Nothing to import: those registries are already stored.");
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
        case "importFailed":
            return i18n("Some credentials could not be imported.");
        default:
            return "";
        }
    }

    /*! 钱包不可用的原因 key → 说明。 */
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

    /*! 打开登录对话框（可带预填仓库）。 */
    function openLoginDialog(serverAddress: string): void {
        loginDialog.reset(serverAddress);
        page.auth.clearResult();
        loginDialog.open();
    }

    function resetImportSelection(): void {
        const selection = ({});
        for (const address of page.auth.importableAddresses) {
            selection[address] = true;
        }
        page.importSelection = selection;
    }

    function selectedAddresses(): var {
        const result = [];
        for (const address of Object.keys(page.importSelection)) {
            if (page.importSelection[address]) {
                result.push(address);
            }
        }
        return result;
    }

    Connections {
        target: page.auth

        function onImportScanChanged() {
            page.resetImportSelection();
        }

        function onResultChanged() {
            // 登录成功后关闭对话框（失败则把原因留在对话框里）
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

                /* ---------------- 结果 / 错误 ---------------- */
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

                /* ---------------- 钱包状态 ---------------- */
                Kirigami.InlineMessage {
                    objectName: "walletBanner"
                    Layout.fillWidth: true
                    visible: page.auth.walletStateKey !== "ready"
                    type: page.auth.walletStateKey === "unavailable" ? Kirigami.MessageType.Error : Kirigami.MessageType.Information
                    text: page.auth.walletStateKey === "opening"
                        ? i18n("Waiting for KWallet to be unlocked…")
                        : page.walletReasonText()
                    // 动作数组直接内联（QML 不接受"三元 + 对象数组字面量"），
                    // 是否需要它由 Action.visible 决定
                    actions: [
                        Kirigami.Action {
                            text: i18n("Try again")
                            icon.name: "view-refresh"
                            visible: page.auth.walletStateKey === "unavailable"
                            onTriggered: page.auth.refresh()
                        }
                    ]
                }

                /* ---------------- 已保存的仓库 ---------------- */
                Kirigami.Heading {
                    Layout.fillWidth: true
                    level: 3
                    text: i18n("Stored credentials")
                }

                Components.EmptyPlaceholder {
                    objectName: "credentialsEmptyPlaceholder"
                    Layout.fillWidth: true
                    message: page.auth.credentials.empty ? i18n("No credentials stored.") : ""
                    explanationText: page.auth.credentials.empty
                        ? i18n("Log in to a registry to pull private images. Credentials are kept in KWallet only.")
                        : ""
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

                /* ---------------- 从 docker CLI 导入 ---------------- */
                Kirigami.Separator {
                    Layout.fillWidth: true
                }

                Kirigami.Heading {
                    Layout.fillWidth: true
                    level: 3
                    text: i18n("Import from the Docker CLI")
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    text: i18n("Read-only import from %1. The file itself is never modified.", page.auth.cliConfigPath)
                    font: Kirigami.Theme.smallFont
                    opacity: 0.8
                    wrapMode: Text.WordWrap
                }

                Kirigami.InlineMessage {
                    objectName: "cliConfigMissingMessage"
                    Layout.fillWidth: true
                    visible: !page.auth.cliConfigPresent
                    type: Kirigami.MessageType.Information
                    text: i18n("No Docker CLI configuration was found (or it could not be read).")
                }

                Kirigami.InlineMessage {
                    objectName: "helperManagedMessage"
                    Layout.fillWidth: true
                    visible: page.auth.helperManagedKeys.length > 0
                    type: Kirigami.MessageType.Information
                    text: i18n("Some credentials are managed by an external credential helper (%1). Kontainer does not run external credential programs; log in to those registries manually.",
                              page.auth.helperManagedKeys.join(", "))
                }

                QQC2.Label {
                    objectName: "skippedImportKeysLabel"
                    Layout.fillWidth: true
                    visible: page.auth.skippedImportKeys.length > 0
                    text: i18np("One entry in the Docker CLI configuration cannot be imported.",
                                "%1 entries in the Docker CLI configuration cannot be imported.",
                                page.auth.skippedImportKeys.length)
                    font: Kirigami.Theme.smallFont
                    opacity: 0.8
                    wrapMode: Text.WordWrap
                }

                Repeater {
                    model: page.auth.importableAddresses

                    delegate: QQC2.CheckBox {
                        id: importCheck

                        required property string modelData

                        objectName: "importCandidateCheck"
                        Layout.fillWidth: true
                        text: importCheck.modelData
                        checked: page.importSelection[importCheck.modelData] === true
                        onToggled: {
                            const selection = Object.assign({}, page.importSelection);
                            selection[importCheck.modelData] = importCheck.checked;
                            page.importSelection = selection;
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.Button {
                        objectName: "importSelectedButton"
                        text: i18n("Import selected")
                        icon.name: "document-import"
                        enabled: page.auth.walletStateKey === "ready" && page.importCount > 0
                        onClicked: page.auth.importFromCli(page.selectedAddresses())
                    }

                    Item {
                        Layout.fillWidth: true
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
