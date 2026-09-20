/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Runtime configuration page (ARCH_V5_V8 §2.2–§2.4).

    The project's first "writes system configuration" UI, so three things share one screen:

      - **Current state**: deployment form, config file path and writability, other keys (read-only)
      - **What can change**: registry mirrors, insecure registries, concurrent downloads, log driver
      - **When the write fails**: an unlock entry point when privilege is needed, or a copyable
        command when no helper exists

    The page validates nothing and builds no JSON: validation goes through the single `Presentation`
    implementation, merging and writing through `DaemonConfigController`, which preserves unknown keys.
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kcmutils as KCM
import org.kde.kirigami as Kirigami
import org.kde.kcm.docker as Kontainer

import "components" as Components

KCM.AbstractKCM {
    id: page

    /*! `user` (no privilege needed) or `system` (protected area). Decided by the entry point. */
    property string scope: "system"

    readonly property var controller: page.scope === "user"
        ? kcm.controller.daemonConfigUser
        : kcm.controller.daemonConfigSystem
    /*! Protected scope and the current user cannot write the file: unlocking needed. */
    readonly property bool protectedScope: page.controller.requiresPrivilege
    /*! Whether an escalation path exists (helper/policy installed). */
    readonly property bool privilegeAvailable: page.controller.privilegeAvailable
    /*! Whether fields are editable: the protected scope must be unlocked first. */
    readonly property bool editable: !page.protectedScope || page.controller.unlocked
    readonly property var engine: kcm.controller.engine
    readonly property real contentMaxWidth: Kirigami.Units.gridUnit * 42
    /*! Label column width for editable rows (aligned with read-only rows and KeyValueListEditor). */
    readonly property real labelColumnWidth: Kirigami.Units.gridUnit * 10

    /*! UI state for authorization / fallback. */
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

    /*! Content of the read-only info block (KeyValueList consumes a model with label/value roles). */
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
            label: i18n("Needs administrator rights to change the file"),
            value: page.controller.requiresPrivilege ? i18n("Yes") : i18n("No")
        });

        settingsRows.clear();
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
            // The example address is a parameter, not part of the msgid: gettext discourages URLs in
            // translatable strings (nothing to translate, and translators may mangle them).
            // It must stay a plain QML string literal — QStringLiteral is a C++ macro and throws
            // ReferenceError in QML (hit for real, right after adding an empty line)
            return i18n("Enter a registry mirror address, for example %1", "https://mirror.example.com");
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
                // Confirm first when there are unsaved changes: this is the only entry point
                // that still drops edits (auto-refresh no longer re-reads the disk;
                // see DaemonConfigController::setEngineInfo)
                if (page.controller.dirty) {
                    discardChangesDialog.open();
                    return;
                }
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

                /* ---------------- Current state ---------------- */
                Kirigami.Heading {
                    Layout.fillWidth: true
                    Layout.topMargin: Kirigami.Units.smallSpacing
                    level: 2
                    text: page.scope === "user"
                        ? i18n("User runtime configuration")
                        : i18n("System runtime configuration")
                }

                /* General protected-scope warning, worded per scope: system-level means "you are changing
                   the whole machine"; user-level only "this file is not yours to write"
                   (e.g. it was created with sudo) */
                Kirigami.InlineMessage {
                    objectName: "protectedScopeBanner"
                    Layout.fillWidth: true
                    visible: page.protectedScope
                    type: Kirigami.MessageType.Warning
                    text: i18n("Making changes take effect needs administrator rights.")
                }

                /* This file is not the one the running daemon reads: changes will not take effect */
                Kirigami.InlineMessage {
                    objectName: "inactiveScopeBanner"
                    Layout.fillWidth: true
                    visible: !page.controller.activeScope
                    type: Kirigami.MessageType.Information
                    text: page.scope === "user"
                        /*
                         * User feedback: this page could show three long "needs administrator rights"
                         * notices at once — repetitive and space-hungry. Collapsed into one sentence;
                         * the lock button tooltip keeps the detail.
                         */
                        ? i18n("Making changes take effect needs administrator rights.")
                        : i18n("Making changes take effect needs administrator rights.")
                }

                /* Unlock state */
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

                // Data root in the user's home but a system-service daemon: easily mistaken for rootless
                Kirigami.InlineMessage {
                    objectName: "dataRootHint"
                    Layout.fillWidth: true
                    visible: page.controller.dataRootInHomeDir && page.controller.formKey === "systemRoot"
                    type: Kirigami.MessageType.Information
                    text: i18n("Making changes take effect needs administrator rights.")
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

                /* ---------------- Editable settings ---------------- */
                Kirigami.Separator {
                    Layout.fillWidth: true
                }

                Kirigami.Heading {
                    Layout.fillWidth: true
                    level: 3
                    text: i18n("Registry mirrors")
                }

                Components.StringListEditor {
                    id: mirrorsEditor

                    objectName: "mirrorsEditor"
                    Layout.fillWidth: true
                    initialEntries: page.controller.registryMirrors
                    editable: page.editable
                    placeholderText: "https://mirror.example.com" // i18n-lint: allow example address (data, not translated)
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
                    placeholderText: "registry.local:5000" // i18n-lint: allow example address (data, not translated)
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

                /* The two editable fields use a two-column grid, not Kirigami.FormLayout, which would
                   center the whole form while everything else on this page is left-aligned */
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: Kirigami.Units.largeSpacing
                    rowSpacing: Kirigami.Units.smallSpacing

                    QQC2.Label {
                        Layout.preferredWidth: page.labelColumnWidth
                        text: i18n("Concurrent downloads")
                    }

                    QQC2.SpinBox {
                        id: concurrentDownloadsSpin

                        objectName: "concurrentDownloadsSpin"
                        // 0 = use the daemon default (on save the key is removed, not written as 0)
                        from: 0
                        to: 1024
                        // `enabled` governs the whole control: with only `editable` set the text field is
                        // read-only but the up/down arrows still change the value (not allowed while locked)
                        enabled: page.editable
                        editable: page.editable
                        value: page.controller.maxConcurrentDownloads
                        textFromValue: function (value) {
                            return value === 0 ? i18n("default") : String(value);
                        }
                        valueFromText: function (text) {
                            const parsed = parseInt(text, 10);
                            return isNaN(parsed) ? 0 : parsed;
                        }
                        // onValueModified fires on user edits only; programmatic assignment
                        // (disk read / post-save refresh) does not
                        onValueModified: page.controller.setMaxConcurrentDownloads(value)
                        Accessible.name: i18n("Concurrent downloads")
                    }

                    QQC2.Label {
                        Layout.preferredWidth: page.labelColumnWidth
                        text: i18n("Log driver")
                    }

                    QQC2.ComboBox {
                        id: logDriverCombo

                        objectName: "logDriverCombo"
                        Layout.preferredWidth: Kirigami.Units.gridUnit * 12
                        enabled: page.editable
                        // The list comes from the helper's whitelist, first entry empty = default;
                        // the UI keeps no copy of its own
                        model: page.controller.selectableLogDrivers()
                        textRole: ""
                        displayText: currentIndex === 0 ? i18n("default") : currentText
                        currentIndex: {
                            const index = page.controller.selectableLogDrivers().indexOf(page.controller.logDriver);
                            return index >= 0 ? index : 0;
                        }
                        // onActivated fires only on user selection (programmatic currentIndex changes do not)
                        onActivated: page.controller.setLogDriver(currentIndex === 0 ? "" : currentText)
                        Accessible.name: i18n("Log driver")
                    }
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

                /* ---------------- When writing is not possible ---------------- */
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

        /* ---------------- Bottom actions ---------------- */
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

    /*! Reloading from disk discards unsaved edits: ask once before dropping them. */
    Components.ConfirmDialog {
        id: discardChangesDialog

        objectName: "discardChangesDialog"
        headingText: i18n("Discard the unsaved changes?")
        questionText: i18n("Reloading reads the file from disk again and drops what you have changed here.")
        consequenceText: i18n("The file on disk is not touched.")
        acceptText: i18n("Discard and reload")
        destructive: true
        onConfirmed: {
            page.controller.reload();
            saveMessage.visible = false;
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
