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

    readonly property var controller: kcm.controller.daemonConfig
    readonly property var engine: kcm.controller.engine
    readonly property real contentMaxWidth: Kirigami.Units.gridUnit * 42

    /*! 授权/降级相关的界面状态。 */
    property bool showManualCommands: false

    signal closeRequested

    objectName: "daemonConfigPage"

    Component.onCompleted: {
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
            return i18n("Enter a registry mirror address, for example https://mirror.example.com");
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
                    text: i18n("Docker runtime configuration")
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
                    Layout.fillWidth: true
                    visible: page.controller.requiresPrivilege
                    type: Kirigami.MessageType.Information
                    text: i18n("This file belongs to the system, so saving requires administrator rights.")
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
                    visible: page.showManualCommands
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
                objectName: "saveConfigButton"
                text: i18n("Save")
                icon.name: "document-save"
                enabled: page.controller.dirty && page.controller.parseError.length === 0
                    && !mirrorsEditor.hasErrors() && !insecureEditor.hasErrors()
                onClicked: page.saveAndReport()
            }

            QQC2.Button {
                objectName: "restoreBackupButton"
                text: i18n("Restore previous version")
                icon.name: "document-revert"
                enabled: page.controller.backups.length > 0
                onClicked: restoreDialog.open()
            }

            Item {
                Layout.fillWidth: true
            }

            QQC2.Label {
                visible: page.controller.dirty && !page.controller.requiresPrivilege
                text: i18n("Unsaved changes")
                font: Kirigami.Theme.smallFont
                opacity: 0.7
            }
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
