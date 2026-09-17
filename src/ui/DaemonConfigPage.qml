/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    运行时配置页（ARCH_V5_V8 §2.2–§2.4）。

    这是本项目第一个"写系统配置"的界面，因此把三件事摆在同一屏上：

      - **现状**：部署形态、配置文件路径与是否可写、其他键（只读）
      - **可改的东西**：镜像加速器、不安全仓库、并发下载数、日志驱动
      - **写不进去时怎么办**：需要提权时给出授权入口；helper 不可用时给出可直接复制的命令

    页面本身不做校验、不拼 JSON：校验走 `Presentation` 的单一实现，
    合并与写入走 `DaemonConfigController`（未知键由它逐键保留）。
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kcmutils as KCM
import org.kde.kirigami as Kirigami
import org.kde.kontainer as Kontainer

import "components" as Components

KCM.AbstractKCM {
    id: page

    /*! `user`（不需要提权）或 `system`（受保护区）。由入口决定。 */
    property string scope: "system"

    readonly property var controller: page.scope === "user"
        ? kcm.controller.daemonConfigUser
        : kcm.controller.daemonConfigSystem
    /*! 受保护区且当前用户写不了这个文件：需要解锁。 */
    readonly property bool protectedScope: page.controller.requiresPrivilege
    /*! 是否有提权通路（helper/policy 已安装）。 */
    readonly property bool privilegeAvailable: page.controller.privilegeAvailable
    /*! 字段是否可编辑：受保护区必须已解锁。 */
    readonly property bool editable: !page.protectedScope || page.controller.unlocked
    readonly property var engine: kcm.controller.engine
    readonly property real contentMaxWidth: Kirigami.Units.gridUnit * 42

    /*! 授权/降级相关的界面状态。 */
    property bool showManualCommands: false

    signal closeRequested

    objectName: "daemonConfigPage"

    Component.onCompleted: {
        controller.setScope(page.scope);
        controller.reload();
        page.refreshRows();
    }

    Connections {
        target: page.controller
        function onChanged() {
            page.refreshRows();
        }
    }

    ListModel {
        id: deploymentRows
    }

    ListModel {
        id: settingsRows
    }

    /*! 只读信息块的内容（KeyValueList 消费的是带 label/value 角色的模型）。 */
    function refreshRows(): void {
        deploymentRows.clear();
        deploymentRows.append({
            label: i18n("Deployment"),
            value: page.controller.formKey === "rootless"
                ? i18n("Rootless (user-level daemon)")
                : page.controller.formKey === "systemRoot"
                    ? i18n("System service (root daemon)")
                    : i18n("Unknown")
        });
        deploymentRows.append({
            label: i18n("Configuration file"),
            value: page.controller.configPath
        });
        deploymentRows.append({
            label: i18n("Writable by the current user"),
            value: page.controller.configWritable ? i18n("Yes") : i18n("No")
        });
        deploymentRows.append({
            label: i18n("Needs administrator rights to change"),
            value: page.controller.requiresPrivilege ? i18n("Yes") : i18n("No")
        });

        settingsRows.clear();
        settingsRows.append({
            label: i18n("Concurrent downloads"),
            value: page.controller.maxConcurrentDownloads > 0 ? String(page.controller.maxConcurrentDownloads) : i18n("default")
        });
        settingsRows.append({
            label: i18n("Log driver"),
            value: page.controller.logDriver.length > 0 ? page.controller.logDriver : i18n("default")
        });
        settingsRows.append({
            label: i18n("Data directory"),
            value: page.controller.dataRoot.length > 0 ? page.controller.dataRoot : i18n("default")
        });
        settingsRows.append({
            label: i18n("Storage driver"),
            value: page.controller.configuredStorageDriver.length > 0 ? page.controller.configuredStorageDriver : i18n("from the engine")
        });
        settingsRows.append({
            label: i18n("Mirrors currently in effect"),
            value: page.controller.activeRegistryMirrors.length > 0
                ? page.controller.activeRegistryMirrors.join(", ")
                : i18n("none")
        });
    }

    function mirrorError(value: string): string {
        const key = Kontainer.Presentation.registryMirrorErrorKey(value);
        if (key === "emptyHost") {
            // 示例地址走参数而不是写进 msgid：gettext 不建议把 URL 放进待译字符串
            //（URL 不需要翻译，混在里面只会让译者去改动它）
            return i18n("Enter a registry mirror address, for example %1", QStringLiteral("https://mirror.example.com"));
        }
        if (key === "invalid") {
            return i18n("Use an address of the form http(s)://host[:port].");
        }
        return "";
    }

    function registryError(value: string): string {
        const key = Kontainer.Presentation.insecureRegistryErrorKey(value);
        if (key === "invalid") {
            return i18n("Use host[:port] only, without a scheme or path.");
        }
        return "";
    }

    function saveAndReport(): void {
        if (page.controller.save()) {
            saveMessage.visible = true;
            saveMessage.type = Kirigami.MessageType.Positive;
            saveMessage.text = page.controller.restartPending
                ? i18n("Configuration written. Docker must be restarted before the changes take effect.")
                : i18n("Configuration written.");
        }
    }

    actions: [
        Kirigami.Action {
            text: i18n("Reload from disk")
            icon.name: "view-refresh"
            onTriggered: {
                page.controller.reload();
                saveMessage.visible = false;
            }
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

                /* ---------------- 现状 ---------------- */
                Kirigami.Heading {
                    Layout.fillWidth: true
                    Layout.topMargin: Kirigami.Units.smallSpacing
                    level: 2
                    text: page.scope === "user"
                        ? i18n("User runtime configuration")
                        : i18n("System runtime configuration")
                }

                /* 受保护区的总警示：措辞按作用域分开——系统级是"改的是整台机器"，
                   用户级只是"这个文件不归你写"（例如曾经用 sudo 建过） */
                Kirigami.InlineMessage {
                    objectName: "protectedScopeBanner"
                    Layout.fillWidth: true
                    visible: page.protectedScope
                    type: Kirigami.MessageType.Warning
                    text: page.scope === "user"
                        ? i18n("This file is not writable by the current user (it may belong to another user), so changing it needs administrator rights.")
                        : i18n("These settings affect the Docker daemon for the whole machine and every user. Changing them needs administrator rights.")
                }

                /* 这个文件不是正在运行的 daemon 读的那个：改了不会生效 */
                Kirigami.InlineMessage {
                    objectName: "inactiveScopeBanner"
                    Layout.fillWidth: true
                    visible: !page.controller.activeScope
                    type: Kirigami.MessageType.Information
                    text: page.scope === "user"
                        ? i18n("The running Docker daemon is a system service, so it does not read this file. Changes here only affect a rootless daemon.")
                        : i18n("The running Docker daemon is rootless, so it does not read this file. Changes here only affect a system-wide daemon.")
                }

                /* 解锁状态 */
                Kirigami.InlineMessage {
                    objectName: "lockedMessage"
                    Layout.fillWidth: true
                    visible: page.protectedScope && !page.controller.unlocked && page.privilegeAvailable
                    type: Kirigami.MessageType.Information
                    text: i18n("Settings are read-only until you unlock them. Unlocking asks for administrator rights; the authorization lasts about five minutes.")
                }

                Kirigami.InlineMessage {
                    objectName: "unlockedMessage"
                    Layout.fillWidth: true
                    visible: page.controller.unlocked
                    type: Kirigami.MessageType.Positive
                    text: i18n("Unlocked. Saving and restarting will not ask again for about %1 seconds.", page.controller.unlockSecondsRemaining)
                }

                Components.KeyValueList {
                    Layout.fillWidth: true
                    model: deploymentRows
                }

                // 数据目录落在用户家目录、但 daemon 是系统服务：容易被误认为 rootless
                Kirigami.InlineMessage {
                    objectName: "dataRootHint"
                    Layout.fillWidth: true
                    visible: page.controller.dataRootInHomeDir && page.controller.formKey === "systemRoot"
                    type: Kirigami.MessageType.Information
                    text: i18n("The Docker data directory is inside your home directory, but the daemon itself runs as a system service: changing this configuration needs administrator rights.")
                }

                Kirigami.InlineMessage {
                    objectName: "parseErrorMessage"
                    Layout.fillWidth: true
                    visible: page.controller.parseError.length > 0
                    type: Kirigami.MessageType.Error
                    text: i18n("The configuration file could not be parsed (%1). It is shown read-only; Kontainer will not overwrite it.", page.controller.parseError)
                }

                Kirigami.InlineMessage {
                    objectName: "restartPendingMessage"
                    Layout.fillWidth: true
                    visible: page.controller.restartPending
                    type: Kirigami.MessageType.Warning
                    text: page.controller.liveRestoreEnabled
                        ? i18n("The saved registry mirrors are not active yet. Restart the Docker service to apply them (running containers are kept because live-restore is enabled).")
                        : i18n("The saved registry mirrors are not active yet. Restarting the Docker service will stop running containers; those with a restart policy will come back automatically.")
                }

                Kirigami.InlineMessage {
                    id: saveMessage

                    objectName: "saveMessage"
                    Layout.fillWidth: true
                    visible: false
                    showCloseButton: true
                }

                Kirigami.InlineMessage {
                    objectName: "saveErrorMessage"
                    Layout.fillWidth: true
                    visible: page.controller.lastError.length > 0
                    type: page.controller.lastError === "privilegeRequired" ? Kirigami.MessageType.Warning : Kirigami.MessageType.Error
                    text: {
                        switch (page.controller.lastError) {
                        case "privilegeRequired":
                            return i18n("Changing this file needs administrator rights.");
                        case "configUnparsable":
                            return i18n("Saving is disabled because the current file cannot be parsed.");
                        case "nothingToSave":
                            return i18n("Nothing to save: no setting was changed.");
                        case "noBackup":
                            return i18n("There is no backup to restore yet.");
                        case "backupUnreadable":
                            return i18n("That backup cannot be used (it is unreadable or not valid JSON).");
                        case "writeFailed":
                            return i18n("Writing the configuration file failed. The previous file was left unchanged.");
                        case "helperUnavailable":
                            return i18n("Administrator access is not available (the helper or its policy is not installed). Use the command below instead.");
                        case "cancelled":
                            return i18n("Authorization was cancelled; nothing was changed.");
                        case "authorizationDenied":
                            return i18n("Authorization was denied; nothing was changed.");
                        case "invalidRequest":
                            return i18n("The helper rejected the request; nothing was changed.");
                        case "systemdUnavailable":
                            return i18n("Could not reach systemd to restart the Docker service.");
                        case "restartFailed":
                            return i18n("Restarting the Docker service failed.");
                        case "locked":
                            return i18n("Unlock the settings before saving.");
                        default:
                            return "";
                        }
                    }
                }

                /* ---------------- 可改的设置 ---------------- */
                Kirigami.Separator {
                    Layout.fillWidth: true
                }

                Kirigami.Heading {
                    Layout.fillWidth: true
                    level: 3
                    text: i18n("Registry mirrors")
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    text: i18n("Tried in the listed order. A mirror is useful when the registry itself is unreachable from this machine.")
                    font: Kirigami.Theme.smallFont
                    opacity: 0.8
                    wrapMode: Text.WordWrap
                }

                Components.StringListEditor {
                    id: mirrorsEditor

                    objectName: "mirrorsEditor"
                    Layout.fillWidth: true
                    initialEntries: page.controller.registryMirrors
                    editable: page.editable
                    placeholderText: "https://mirror.example.com" // i18n-lint: allow 示例地址（数据，不翻译）
                    addText: i18n("Add mirror")
                    validator: function (value) {
                        return page.mirrorError(value);
                    }
                    onChanged: page.controller.setRegistryMirrors(values())
                }

                Components.StringListEditor {
                    id: insecureEditor

                    objectName: "insecureEditor"
                    Layout.fillWidth: true
                    visible: false
                    initialEntries: page.controller.insecureRegistries
                    editable: page.editable
                    placeholderText: "registry.local:5000" // i18n-lint: allow 示例地址（数据，不翻译）
                    addText: i18n("Add registry")
                    validator: function (value) {
                        return page.registryError(value);
                    }
                    onChanged: page.controller.setInsecureRegistries(values())
                }

                Kirigami.Separator {
                    Layout.fillWidth: true
                }

                Kirigami.Heading {
                    Layout.fillWidth: true
                    level: 3
                    text: i18n("Other settings")
                }

                Components.KeyValueList {
                    Layout.fillWidth: true
                    model: settingsRows
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    visible: page.controller.unmanagedKeys.length > 0
                    text: i18np("Other keys in this file are shown read-only and kept as they are: %1",
                                "Other keys in this file are shown read-only and kept as they are: %1",
                                page.controller.unmanagedKeys.join(", "))
                    font: Kirigami.Theme.smallFont
                    opacity: 0.7
                    wrapMode: Text.WordWrap
                }

                /* ---------------- 写不进去时的出路 ---------------- */
                Kirigami.InlineMessage {
                    objectName: "helperUnavailableMessage"
                    Layout.fillWidth: true
                    visible: page.controller.lastError === "helperUnavailable"
                    type: Kirigami.MessageType.Warning
                    text: i18n("This build has no administrator helper installed, so the change must be applied manually.")
                }

                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    visible: page.controller.requiresPrivilege
                    type: Kirigami.MessageType.Information
                    text: page.scope === "user"
                        ? i18n("This file is not writable by the current user, so saving requires administrator rights.")
                        : i18n("This file belongs to the system, so saving requires administrator rights.")
                    actions: [
                        Kirigami.Action {
                            text: i18n("Show command to run manually")
                            icon.name: "utilities-terminal"
                            onTriggered: page.showManualCommands = !page.showManualCommands
                        }
                    ]
                }

                QQC2.TextArea {
                    objectName: "manualCommandArea"

                    Layout.fillWidth: true
                    visible: page.showManualCommands || page.controller.lastError === "helperUnavailable"
                    readOnly: true
                    text: page.controller.privilegedCommand()
                    font.family: "monospace"
                    wrapMode: TextEdit.NoWrap
                    selectByMouse: true
                }

                Item {
                    Layout.fillHeight: true
                }
            }
        }

        /* ---------------- 底部动作 ---------------- */
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            QQC2.Button {
                objectName: "unlockButton"
                visible: page.protectedScope && !page.controller.unlocked && page.privilegeAvailable
                text: i18n("Unlock to edit")
                icon.name: "lock-open"
                onClicked: page.controller.requestUnlock()
            }

            QQC2.Button {
                objectName: "lockButton"
                visible: page.controller.unlocked
                text: i18n("Lock again")
                icon.name: "lock"
                onClicked: page.controller.lock()
            }

            QQC2.Button {
                objectName: "saveConfigButton"
                text: i18n("Save")
                icon.name: "document-save"
                enabled: page.editable && page.controller.dirty && page.controller.parseError.length === 0
                    && !mirrorsEditor.hasErrors() && !insecureEditor.hasErrors()
                onClicked: page.saveAndReport()
            }

            QQC2.Button {
                objectName: "restartDockerButton"
                text: i18n("Restart Docker…")
                icon.name: "system-reboot"
                enabled: !page.protectedScope || page.controller.unlocked
                onClicked: restartDialog.open()
            }

            QQC2.Button {
                objectName: "restoreBackupButton"
                text: i18n("Restore previous version")
                icon.name: "document-revert"
                enabled: page.editable && page.controller.backups.length > 0
                onClicked: restoreDialog.open()
            }

            Item {
                Layout.fillWidth: true
            }

            QQC2.Label {
                Layout.fillWidth: true
                visible: page.editable
                text: i18n("Writes to %1 (a backup is kept before every save).", page.controller.configPath)
                font: Kirigami.Theme.smallFont
                opacity: 0.7
                elide: Text.ElideMiddle
            }

            QQC2.Label {
                visible: page.controller.dirty && page.editable
                text: i18n("Unsaved changes")
                font: Kirigami.Theme.smallFont
                opacity: 0.7
            }
        }
    }

    Components.ConfirmDialog {
        id: restartDialog

        objectName: "restartDockerDialog"
        headingText: i18n("Restart Docker")
        questionText: i18n("Restart the Docker service now?")
        consequenceText: page.controller.liveRestoreEnabled
            ? i18n("Running containers are kept because live-restore is enabled.")
            : i18np("One running container will be stopped; it comes back automatically if its restart policy says so.",
                    "%1 running containers will be stopped; they come back automatically if their restart policy says so.",
                    page.controller.runningContainers)
        acceptText: i18n("Restart")
        destructive: true
        onConfirmed: page.controller.restartDocker()
    }

    Connections {
        target: page.controller
        function onRestarted(success, errorKey) {
            saveMessage.visible = true;
            saveMessage.type = success ? Kirigami.MessageType.Positive : Kirigami.MessageType.Error;
            saveMessage.text = success
                ? i18n("Docker restarted. The page refreshes once the engine is reachable again.")
                : i18n("Restarting Docker failed (%1).", errorKey);
        }
    }

    Components.ConfirmDialog {
        id: restoreDialog

        objectName: "restoreConfigDialog"
        headingText: i18n("Restore previous version")
        questionText: i18n("Replace the current configuration with the most recent backup?")
        consequenceText: i18n("The current file is backed up first, so this can be undone the same way.")
        acceptText: i18n("Restore")
        onConfirmed: {
            if (page.controller.restoreBackup("")) {
                saveMessage.visible = true;
                saveMessage.type = Kirigami.MessageType.Positive;
                saveMessage.text = i18n("Previous configuration restored. Docker must be restarted to apply it.");
            }
        }
    }
}
