/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    仓库登录对话框（ARCH_V5_V8 §2.7）。

    三条硬规则（都在注释里写明，改动时别破坏）：

      1. **密码不回显、不进剪贴板**：`echoMode: Password`（Qt 会同时禁止复制），
         并且这里没有 CopyButton——导出凭据只能通过"登录"动作。
      2. **只在"登录"被点击后把凭据交给控制器**，控制器负责 `POST /auth` 校验，
         校验成功才写钱包。对话框自己不保存任何东西，关闭即丢弃。
      3. **错误就地显示**：校验失败的原因必须出现在对话框里（用户正看着它），
         而不是只在他身后的页面上。
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

Kirigami.Dialog {
    id: dialog

    /*! 默认仓库（Docker Hub 的历史写法，与 config.json 一致）。 */
    readonly property string defaultServerAddress: "https://index.docker.io/v1/"

    /*! 预填的仓库地址（例如从拉取失败的镜像引用带过来）。 */
    property string presetServerAddress: ""

    /*! 最近一次校验失败的文案（由调用方从控制器的 key 映射后回填）。 */
    property string errorText: ""
    /*! 校验进行中：按钮禁用，避免重复提交。 */
    property bool busy: false

    signal loginRequested(string serverAddress, string username, string password, string token)
    signal dismissed

    objectName: "registryLoginDialog"

    title: i18n("Log in to a registry")
    standardButtons: Kirigami.Dialog.NoButton
    preferredWidth: Kirigami.Units.gridUnit * 24

    /*! 打开前重置：不复用上一次的输入（尤其是密码）。 */
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

        QQC2.Label {
            Layout.fillWidth: true
            text: i18n("Credentials are stored in KWallet, never in a plain-text file.")
            font: Kirigami.Theme.smallFont
            opacity: 0.8
            wrapMode: Text.WordWrap
        }

        QQC2.TextField {
            id: serverField

            objectName: "loginServerField"
            Layout.fillWidth: true
            enabled: !dialog.busy
            placeholderText: "https://index.docker.io/v1/" // i18n-lint: allow 示例地址（数据，不翻译）
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
            // 不回显、也不提供复制入口（Qt 在密码模式下同时禁止复制）
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
