/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Create-container wizard (ARCH_V5_V8 §4.4).

    A stepwise form, but **all state and validation live in `CreateContainerController`** (C++): half
    the validation matrix compares against backend data (duplicate names, port conflicts, whether an
    image is local), which QML could not test and would fork from the controller's rules. This page
    does three things only: write input into the controller, lay out the form for
    `controller.stepKey`, and render the summary.

    The last step is a **read-only summary**: environment variables list key names only (values may be
    secrets), and nothing is submitted before confirmation.
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.kcm.docker as Kontainer

import "components" as Components

Kirigami.Page {
    id: page

    /*! Image reference preselected when entering from an image card or detail page. */
    property string presetImage: ""
    /*! Clone: prefill the form from this container ID. */
    property string cloneFromContainerId: ""

    readonly property var controller: kcm.controller.createContainer
    readonly property var operations: kcm.controller.operations

    signal closeRequested
    /*! Created successfully: jump to the new container's detail page. */
    signal containerCreated(string containerId)

    objectName: "createContainerPage"

    /*! Whether the preset manager panel is open (inside the mounts step). */
    property bool presetPanelOpen: false

    /*! Reason key when a step-button jump was rejected (empty = none). */
    property string stepJumpErrorKey: ""

    /*!
     * **View** state of the privileged checkbox.
     *
     * Do not bind `checked` to the controller and then assign it in a handler: that breaks the
     * binding, so a later controller change to true never syncs (user report F4: the box stayed
     * empty after confirming).
     */
    property bool privilegedVisual: false

    Connections {
        target: page.controller
        // The controller is the single source of truth: realign the view when it changes
        function onChanged() {
            page.privilegedVisual = page.controller.privileged;
            /*
             * Also clear the "jump rejected" reason.
             *
             * User report: with an image already chosen, "Choose an image." kept showing (only
             * clicking a tab cleared it). That banner came from stepJumpErrorKey, which was cleared
             * only on a **successful step-button click** — neither Next nor editing the form cleared
             * it. Now any form change clears it, so the banner falls back to the current step's real
             * state (stepErrorKey) and vanishes when it should.
             */
            page.stepJumpErrorKey = "";
        }
    }

    /*!
     * Relay object for delegates (fix from phase 8).
     *
     * Referencing the **root object's id** (page) inside a `Repeater` delegate throws
     * `ReferenceError: page is not defined` — the user-reported "cannot delete port mappings /
     * cannot add mounts": the handler called `page.pushPorts()`, threw, and the edit never reached
     * the controller. Ids of **non-root** objects in the same file do work in delegates, so the
     * capabilities delegates need (controller + row sync) are relayed here once.
     */
    QtObject {
        id: wizard

        readonly property var controller: page.controller
        readonly property var operations: page.operations

        /*! Port/mount edits go to the controller (rules in C++); delegates report row and field only. */
        function addPort() {
            wizard.controller.addPortRow(80, 0, "", "tcp");
        }
        function setPort(row, field, value) {
            wizard.controller.setPortRow(row, field, value);
        }
        function removePort(row) {
            wizard.controller.removePortRow(row);
        }
        function addMount() {
            wizard.controller.addMountRow("bind", "", "", false);
        }
        function setMount(row, field, value) {
            wizard.controller.setMountRow(row, field, value);
        }
        function removeMount(row) {
            wizard.controller.removeMountRow(row);
        }
        function addPreset(presetId) {
            wizard.controller.addMountFromPreset(presetId);
        }
    }

    /*! Step key → title (the controller owns the order; the UI keeps no copy). */
    function stepTitle(key: string): string {
        switch (key) {
        case "image":
            return i18n("Image");
        case "basics":
            return i18n("Basics");
        case "environment":
            return i18n("Environment & labels");
        case "interactive":
            return i18n("Interactive");
        case "ports":
            return i18n("Ports");
        case "mounts":
            return i18n("Mounts");
        case "resources":
            return i18n("Resources");
        default:
            return i18n("Review");
        }
    }

    /*! Validation key → message. */
    function errorText(key: string): string {
        switch (key) {
        case "imageRequired":
            return i18n("Choose an image.");
        case "imageNotLocal":
            return i18n("This image is not available locally. Pull it first, or tick “Pull it first”.");
        case "nameRequired":
            return i18n("Enter a name for the container.");
        case "nameInvalid":
            return i18n("Use letters, digits, underscore, dot or dash, and do not start with a separator.");
        case "nameInUse":
            return i18n("A container with this name already exists.");
        case "portRequired":
            return i18n("Every port row needs a container port.");
        case "portRange":
            return i18n("Host ports must be between 1 and 65535 (leave empty for a random port).");
        case "portInUse":
            return i18n("This host port is already published by another container.");
        case "portDuplicateInRequest":
            return i18n("The same host port is used by more than one row. One host port cannot serve several container ports.");
        case "keyRequired":
            return i18n("Every environment or label row needs a key.");
        case "keyInvalid":
            return i18n("Keys may contain letters, digits and underscore, and must not start with a digit.");
        case "pathRequired":
            return i18n("Every mount needs a container path.");
        case "pathNotAbsolute":
            return i18n("The container path must be absolute.");
        case "destinationDuplicate":
            return i18n("The same container path is used twice.");
        case "sourceRequired":
            return i18n("Every mount needs a source (host path or volume name).");
        case "sourceNotAbsolute":
            return i18n("Bind mounts need an absolute host path.");
        case "volumeNameInvalid":
            return i18n("Volume names may contain letters, digits, underscore, dot or dash.");
        case "memoryTooSmall":
            return i18n("The memory limit must be at least 6 MiB (or empty for no limit).");
        case "memoryNegative":
            return i18n("The memory limit cannot be negative.");
        case "cpusNegative":
            return i18n("The CPU limit cannot be negative.");
        default:
            return "";
        }
    }

    Connections {
        target: page.controller
        function onStepChanged() {
            page.stepJumpErrorKey = "";
        }
    }

    Connections {
        target: page.controller

        /*
         * Refresh the container list when entering the "ports" step.
         *
         * Port conflicts are decided from **published ports** (`hostPortHolder()`), and the
         * container list polls every 5 s. A container started elsewhere a moment ago may be missing
         * from the stale list, so early interception fails and the error only surfaces at start:
         * `Bind for 0.0.0.0:8100 failed: port is already allocated`.
         */
        function onStepKeyChanged() {
            if (page.controller.stepKey === "ports") {
                kcm.controller.refresh();
            }
        }
    }

    Component.onCompleted: {
        // The network list refreshes only when the Networks tab is entered, so the wizard refetches
        // it: otherwise no network can be picked without visiting that tab first (user report ②)
        if (page.controller.availableNetworks.length === 0) {
            kcm.controller.refreshNetworks();
        }
        if (page.cloneFromContainerId.length > 0) {
            page.controller.prefillFromContainer(page.cloneFromContainerId);
        } else {
            page.controller.reset(page.presetImage);
        }
        page.syncModels();
    }

    Connections {
        target: page.controller

        function onSubmitted(containerId) {
            page.containerCreated(containerId);
        }
    }

    /* Step indicator: finished steps are clickable for review or editing, unfinished ones are not */
    header: QQC2.ToolBar {
        contentItem: RowLayout {
            spacing: Kirigami.Units.smallSpacing

            Repeater {
                model: page.controller.stepKeys

                delegate: QQC2.Button {
                    id: stepButton

                    required property string modelData
                    required property int index

                    objectName: "wizardStepButton"
                    flat: true
                    /*
                     * Follows the current step read-only: deliberately **not** checkable.
                     *
                     * With checkable, Qt flips checked itself on click; when goToStep() rejects the
                     * jump because validation fails, that flip is never undone, so the user sees
                     * "several steps look selected but the page is still on the first one"
                     * (user report A3).
                     */
                    checked: page.controller.stepIndex === stepButton.index
                    text: (stepButton.index + 1) + ". " + page.stepTitle(stepButton.modelData)
                    onClicked: {
                        // Explain rejections: silently doing nothing made users think the UI was broken
                        if (!page.controller.goToStep(stepButton.modelData)) {
                            page.stepJumpErrorKey = page.controller.stepErrorKey;
                        } else {
                            page.stepJumpErrorKey = "";
                        }
                    }
                }
            }

            Item {
                Layout.fillWidth: true
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Kirigami.Units.smallSpacing

        Kirigami.InlineMessage {
            objectName: "wizardStepError"
            Layout.fillWidth: true
            // One banner covers the current step's problem and the reason a step jump was rejected
            visible: page.controller.stepErrorKey.length > 0 || page.stepJumpErrorKey.length > 0
            type: Kirigami.MessageType.Error
            text: page.errorText(page.stepJumpErrorKey.length > 0 ? page.stepJumpErrorKey : page.controller.stepErrorKey)
        }

        Kirigami.InlineMessage {
            objectName: "wizardOperationError"
            Layout.fillWidth: true
            visible: page.operations.resultKey === "error" && page.operations.resultText.length > 0
            type: Kirigami.MessageType.Error
            text: page.operations.resultText
        }

        QQC2.ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: Math.min(parent.width, Kirigami.Units.gridUnit * 42)
                x: Math.max(0, (parent.width - width) / 2)
                spacing: Kirigami.Units.largeSpacing

                /* ---------------------------- ① Image ---------------------------- */
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: page.controller.stepKey === "image"
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Heading {
                        Layout.fillWidth: true
                        level: 3
                        text: i18n("Which image should the container use?")
                    }

                    QQC2.TextField {
                        id: imageField

                        objectName: "wizardImageField"
                        Layout.fillWidth: true
                        placeholderText: i18n("Image reference, for example alpine:3.19")
                        Accessible.name: i18n("Image reference")
                        text: page.controller.image
                        onTextChanged: page.controller.image = text
                    }

                    QQC2.CheckBox {
                        objectName: "wizardPullIfMissing"
                        checked: page.controller.pullIfMissing
                        text: i18n("Pull it first if it is missing locally")
                        onToggled: page.controller.pullIfMissing = checked
                    }

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("Local images:")
                        font.bold: true
                        visible: page.controller.availableImages.length > 0
                    }

                    // Searchable dropdown (user report F3: clicking through many image versions was too slow)
                    Components.FilteredComboBox {
                        id: imageCombo

                        objectName: "wizardImageCombo"
                        Layout.fillWidth: true
                        visible: page.controller.availableImages.length > 0
                        entries: page.controller.availableImages
                        textRole: "reference"
                        searchPlaceholder: i18n("Search images…")
                        onSelected: function (entry) {
                            page.controller.image = entry.reference;
                        }
                    }

                    Components.EmptyPlaceholder {
                        objectName: "wizardNoLocalImages"
                        Layout.fillWidth: true
                        message: page.controller.availableImages.length === 0
                            ? i18n("No local images. Pull one from the Images tab first.")
                            : ""
                    }
                }

                /* ---------------------------- ② Basics ---------------------------- */
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: page.controller.stepKey === "basics"
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Heading {
                        Layout.fillWidth: true
                        level: 3
                        text: i18n("Name, network and restart policy")
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        QQC2.TextField {
                            id: nameField

                            objectName: "wizardNameField"
                            Layout.fillWidth: true
                            placeholderText: i18n("Container name")
                            Accessible.name: i18n("Container name")
                            text: page.controller.name
                            onTextChanged: page.controller.name = text
                        }

                        QQC2.Button {
                            objectName: "wizardSuggestNameButton"
                            text: i18n("Suggest")
                            // A name is derived from the image when empty (see the controller),
                            // so this is enabled as soon as an image exists
                            enabled: page.controller.suggestedName().length > 0
                            onClicked: {
                                const suggestion = page.controller.suggestedName();
                                if (suggestion.length > 0) {
                                    page.controller.name = suggestion;
                                }
                            }
                        }
                    }

                    QQC2.ComboBox {
                        id: networkCombo

                        objectName: "wizardNetworkCombo"
                        Layout.fillWidth: true
                        textRole: "name"
                        valueRole: "name"
                        model: page.controller.availableNetworks
                        onActivated: page.controller.network = currentText
                        // The dropdown shows exactly the network that will be submitted: never "A looks
                        // selected but empty is submitted". The list arrives asynchronously, so sync on
                        // count changes too (it may still be empty when the page is created)
                        onCountChanged: syncNetworkSelection()
                        onCurrentIndexChanged: syncNetworkSelection()
                        Component.onCompleted: syncNetworkSelection()
                        function syncNetworkSelection(): void {
                            if (networkCombo.count === 0) {
                                return;
                            }
                            // The model arrives asynchronously: fix the index first (currentText may still
                            // be empty), then backfill the controller — otherwise the mismatch persists
                            if (networkCombo.currentIndex < 0) {
                                networkCombo.currentIndex = Math.max(0, networkCombo.indexOfValue(page.controller.network));
                            }
                            if (page.controller.network.length === 0 && networkCombo.currentText.length > 0) {
                                page.controller.network = networkCombo.currentText;
                            }
                        }
                    }

                    QQC2.TextField {
                        objectName: "wizardAliasesField"
                        Layout.fillWidth: true
                        placeholderText: i18n("Network aliases (optional, comma separated)")
                        Accessible.name: i18n("Network aliases")
                        text: page.controller.networkAliasesText
                        onTextChanged: page.controller.networkAliasesText = text
                    }

                    QQC2.ComboBox {
                        id: restartCombo

                        objectName: "wizardRestartCombo"
                        Layout.fillWidth: true
                        textRole: "text"
                        valueRole: "value"
                        model: [
                            {text: i18n("Do not restart automatically"), value: "no"},
                            {text: i18n("Always"), value: "always"},
                            {text: i18n("Unless stopped"), value: "unless-stopped"},
                            {text: i18n("On failure"), value: "on-failure"}
                        ]
                        Component.onCompleted: currentIndex = indexOfValue(page.controller.restartPolicy)
                        onActivated: page.controller.restartPolicy = currentValue
                    }

                    QQC2.CheckBox {
                        objectName: "wizardStartAfterCreate"
                        checked: page.controller.startAfterCreate
                        text: i18n("Start the container after creating it")
                        onToggled: page.controller.startAfterCreate = checked
                    }
                }

                /* ---------------------------- ④ Interactive ---------------------------- */
                /* Split out of "Basics" (user testing): mounts/environment come before command and workdir */
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: page.controller.stepKey === "interactive"
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Heading {
                        Layout.fillWidth: true
                        level: 3
                        text: i18n("Command and entry point (optional)")
                    }

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("Interactive")
                        font.bold: true
                    }

                    // User testing: with default arguments alpine's /bin/sh exits on EOF;
                    // -i -t is what people expect
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        QQC2.CheckBox {
                            objectName: "wizardOpenStdinCheck"
                            text: i18n("Keep standard input open (-i)")
                            checked: page.controller.openStdin
                            onToggled: page.controller.openStdin = checked
                        }
                        QQC2.CheckBox {
                            objectName: "wizardTtyCheck"
                            text: i18n("Allocate a terminal (-t)")
                            checked: page.controller.tty
                            onToggled: page.controller.tty = checked
                        }
                        QQC2.CheckBox {
                            objectName: "wizardStdinOnceCheck"
                            text: i18n("Close stdin after the first client disconnects")
                            checked: page.controller.stdinOnce
                            onToggled: page.controller.stdinOnce = checked
                        }
                    }


                    // Command history (F3): local log plus commands from existing containers,
                    // filled into the field below
                    Components.FilteredComboBox {
                        id: commandHistoryCombo

                        objectName: "wizardCommandHistoryCombo"
                        Layout.fillWidth: true
                        entries: page.controller.commandHistory.entries
                        textRole: "command"
                        searchPlaceholder: i18n("Search previous commands…")
                        placeholder: i18n("Reuse a previous command…")
                        onSelected: function (entry) {
                            page.controller.commandText = entry.command;
                        }
                    }

                    QQC2.TextArea {
                        objectName: "wizardCommandField"
                        Layout.fillWidth: true
                        Layout.preferredHeight: Kirigami.Units.gridUnit * 3
                        placeholderText: i18n("One argument per line, for example:\nsh\n-c\nsleep 3600")
                        Accessible.name: i18n("Command")
                        font.family: "monospace"
                        wrapMode: TextEdit.NoWrap
                        text: page.controller.commandText
                        onTextChanged: page.controller.commandText = text
                    }

                    QQC2.TextField {
                        objectName: "wizardEntrypointField"
                        Layout.fillWidth: true
                        placeholderText: i18n("Entry point (optional, one argument per line)")
                        Accessible.name: i18n("Entry point")
                        text: page.controller.entrypointText.split("\n").join(" ")
                        onTextChanged: page.controller.entrypointText = text
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        QQC2.TextField {
                            objectName: "wizardWorkingDirField"
                            Layout.fillWidth: true
                            placeholderText: i18n("Working directory (optional)")
                            Accessible.name: i18n("Working directory")
                            text: page.controller.workingDirectory
                            onTextChanged: page.controller.workingDirectory = text
                        }
                        QQC2.TextField {
                            objectName: "wizardUserField"
                            Layout.fillWidth: true
                            placeholderText: i18n("User (optional, name or uid:gid)")
                            Accessible.name: i18n("User")
                            text: page.controller.user
                            onTextChanged: page.controller.user = text
                        }
                    }
                }

                /* ---------------------------- ⑤ Ports ---------------------------- */
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: page.controller.stepKey === "ports"
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Heading {
                        Layout.fillWidth: true
                        level: 3
                        text: i18n("Published ports")
                    }


                    Components.PortMappingEditor {
                        objectName: "wizardPortEditor"
                        Layout.fillWidth: true
                        controller: page.controller
                    }
                }

                /* ------------------------ ③ Environment & labels ------------------------ */
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: page.controller.stepKey === "environment"
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Heading {
                        Layout.fillWidth: true
                        level: 3
                        text: i18n("Environment variables")
                    }

                    Components.KeyValueListEditor {
                        id: environmentEditor

                        objectName: "wizardEnvironmentEditor"
                        Layout.fillWidth: true
                        // Values are masked by default: environment variables often hold secrets
                        // (same rule as §40 of phase 4)
                        secretValues: true
                        envPasteEnabled: true
                        onChanged: page.controller.environmentRows = environmentEditor.entries()
                    }

                    Kirigami.Heading {
                        Layout.fillWidth: true
                        level: 3
                        text: i18n("Labels")
                    }

                    Components.KeyValueListEditor {
                        id: labelEditor

                        objectName: "wizardLabelEditor"
                        Layout.fillWidth: true
                        onChanged: page.controller.labelRows = labelEditor.entries()
                    }
                }

                /* ---------------------------- ⑥ Mounts ---------------------------- */
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: page.controller.stepKey === "mounts"
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Heading {
                        Layout.fillWidth: true
                        level: 3
                        text: i18n("Mounts")
                    }

                    // Presets became a **searchable dropdown** (user testing: clicking through
                    // many of them was too slow)
                    Components.FilteredComboBox {
                        id: presetCombo

                        objectName: "wizardPresetCombo"
                        Layout.fillWidth: true
                        visible: page.controller.presets.length > 0
                        entries: page.controller.presets
                        textRole: "label"
                        placeholder: i18n("Add a mount from a preset…")
                        searchPlaceholder: i18n("Search presets…")
                        onSelected: function (entry) {
                            wizard.addPreset(entry.id);
                        }
                    }

                    Repeater {
                        id: mountRepeater

                        model: mountRowsModel

                        delegate: RowLayout {
                            id: mountRow

                            required property int index
                            required property string type
                            required property string source
                            required property string destination
                            required property bool readOnly

                            objectName: "wizardMountRow"
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            QQC2.ComboBox {
                                objectName: "wizardMountType"
                                textRole: "text"
                                valueRole: "value"
                                model: [
                                    {text: i18n("Host path"), value: "bind"},
                                    {text: i18n("Named volume"), value: "volume"},
                                    {text: i18n("tmpfs"), value: "tmpfs"}
                                ]
                                Component.onCompleted: currentIndex = indexOfValue(mountRow.type)
                                onActivated: wizard.setMount(mountRow.index, "type", currentValue)
                            }
                            QQC2.TextField {
                                objectName: "wizardMountSource"
                                Layout.fillWidth: true
                                enabled: mountRow.type !== "tmpfs"
                                placeholderText: mountRow.type === "volume" ? i18n("Volume name") : i18n("Host path")
                                Accessible.name: i18n("Mount source")
                                text: mountRow.source
                                onTextChanged: wizard.setMount(mountRow.index, "source", text)
                            }
                            QQC2.TextField {
                                objectName: "wizardMountDestination"
                                Layout.fillWidth: true
                                placeholderText: i18n("Container path")
                                Accessible.name: i18n("Container path")
                                text: mountRow.destination
                                onTextChanged: wizard.setMount(mountRow.index, "destination", text)
                            }
                            QQC2.CheckBox {
                                objectName: "wizardMountReadOnly"
                                text: i18n("Read-only")
                                checked: mountRow.readOnly
                                onToggled: wizard.setMount(mountRow.index, "readOnly", checked)
                            }
                            QQC2.Button {
                                objectName: "wizardRemoveMount"
                                icon.name: "list-remove"
                                flat: true
                                Accessible.name: i18n("Remove this mount")
                                onClicked: wizard.removeMount(mountRow.index)
                            }
                        }
                    }

                    // Manual entry: one click adds an editable mount row (both paths, presets and
                    // manual entry, stay available)
                    QQC2.Button {
                        objectName: "wizardAddMount"
                        text: i18n("Add mount manually")
                        icon.name: "list-add"
                        onClicked: wizard.addMount()
                    }

                }

                /* ---------------------------- ⑦ Resources ---------------------------- */
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: page.controller.stepKey === "resources"
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Heading {
                        Layout.fillWidth: true
                        level: 3
                        text: i18n("Resources and privileges")
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        QQC2.Label {
                            text: i18n("Memory limit (MiB):")
                        }
                        QQC2.SpinBox {
                            id: memorySpin

                            objectName: "wizardMemorySpin"
                            from: 0
                            to: 1024 * 1024
                            stepSize: 64
                            value: Math.round(page.controller.memoryLimitBytes / (1024 * 1024))
                            textFromValue: function (value) {
                                return value === 0 ? i18n("No limit") : value.toString();
                            }
                            onValueModified: page.controller.memoryLimitBytes = value * 1024 * 1024
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        QQC2.Label {
                            text: i18n("CPU limit (cores):")
                        }
                        QQC2.SpinBox {
                            id: cpuSpin

                            objectName: "wizardCpuSpin"
                            from: 0
                            to: 64
                            stepSize: 1
                            // 0 = no limit; fractional values such as 0.5 must be typed into the field
                            editable: true
                            value: Math.round(page.controller.cpus)
                            textFromValue: function (value) {
                                return value === 0 ? i18n("No limit") : value.toString();
                            }
                            onValueModified: page.controller.cpus = value
                        }
                    }

                    QQC2.CheckBox {
                        id: privilegedCheck

                        objectName: "wizardPrivilegedCheck"
                        /*
                         * Deliberately **not** using CheckBox's own check mechanism (checkable: false).
                         *
                         * User report F4: the box stayed empty after confirming. CheckBox assigns
                         * `checked` directly on click, breaking the `checked: …` binding, so later data
                         * changes never sync. With checkable off, a click only toggles intent and the
                         * checked state comes from `privilegedVisual` (hence the controller), leaving
                         * the binding intact.
                         */
                        checkable: false
                        checked: page.privilegedVisual
                        text: i18n("Run with extended privileges (--privileged)")
                        onClicked: {
                            if (page.controller.privileged) {
                                // Already on: turn it off directly (reversible, no confirmation)
                                page.controller.privileged = false;
                            } else {
                                // Off: strong confirmation (the container name must be typed out)
                                privilegedDialog.open();
                            }
                        }
                    }

                    Kirigami.InlineMessage {
                        objectName: "wizardPrivilegedNotice"
                        Layout.fillWidth: true
                        visible: page.controller.privileged
                        type: Kirigami.MessageType.Warning
                        text: i18n("This container runs with the same access as root on the host. Only use it when you know why.")
                    }
                }

                /* ---------------------------- ⑧ Summary ---------------------------- */
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: page.controller.stepKey === "summary"
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Heading {
                        Layout.fillWidth: true
                        level: 3
                        text: i18n("Review and create")
                    }

                    Repeater {
                        id: summaryRepeater

                        objectName: "wizardSummaryRepeater"
                        model: page.controller.summary

                        delegate: RowLayout {
                            required property var modelData

                            objectName: "wizardSummaryRow"
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            QQC2.Label {
                                text: modelData.label
                                Layout.preferredWidth: Kirigami.Units.gridUnit * 10
                                opacity: 0.75
                            }
                            QQC2.Label {
                                objectName: "wizardSummaryValue"
                                Layout.fillWidth: true
                                text: modelData.value
                                font.family: "monospace"
                                wrapMode: Text.WrapAnywhere
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

    /* Port row model: editing one row would rewrite the whole list (the controller owns state), so a
       ListModel acts as an edit buffer and each field change is written straight back to the controller. */
    ListModel {
        id: portRowsModel
    }

    ListModel {
        id: mountRowsModel
    }

    Connections {
        target: page.controller

        function onChanged() {
            syncModels();
        }
    }

    /*! Sync controller rows into the buffer, rebuilding only on difference so typing is not disrupted. */
    function syncModels(): void {
        if (!rowsEqual(portRowsModel, page.controller.portRows)) {
            portRowsModel.clear();
            for (const row of page.controller.portRows) {
                portRowsModel.append({
                    containerPort: row.containerPort ?? 0,
                    hostPort: row.hostPort ?? 0,
                    hostIp: row.hostIp ?? "",
                    protocol: row.protocol ?? "tcp"
                });
            }
        }
        if (!rowsEqual(mountRowsModel, page.controller.mountRows)) {
            mountRowsModel.clear();
            for (const row of page.controller.mountRows) {
                mountRowsModel.append({
                    type: row.type ?? "bind",
                    source: row.source ?? "",
                    destination: row.destination ?? "",
                    readOnly: row.readOnly ?? false
                });
            }
        }
    }

    /*! Whether buffer and controller agree (rebuild only when they differ, sparing the row being typed). */
    function rowsEqual(model, rows): bool {
        if (model.count !== rows.length) {
            return false;
        }
        for (let i = 0; i < model.count; ++i) {
            const item = model.get(i);
            const row = rows[i];
            for (const field of ["containerPort", "hostPort", "hostIp", "protocol",
                                 "type", "source", "destination", "readOnly"]) {
                const left = item[field] ?? "";
                const right = row[field] ?? "";
                if (String(left) !== String(right)) {
                    return false;
                }
            }
        }
        return true;
    }

    Components.ConfirmDialog {
        id: privilegedDialog

        objectName: "wizardPrivilegedDialog"
        headingText: i18n("Run with extended privileges")
        questionText: i18n("Give this container the same access as root on the host?")
        consequenceText: i18n("The container can access all devices and host files. Only continue if you trust the image. Type the container name to confirm.")
        acceptText: i18n("Yes, run privileged")
        destructive: true
        // Strong confirmation: type the container name (agreed design; --privileged cannot use polkit)
        requireText: page.controller.name
        onConfirmed: page.controller.privileged = true
    }

    footer: QQC2.ToolBar {
        contentItem: RowLayout {
            spacing: Kirigami.Units.smallSpacing

            QQC2.Button {
                objectName: "wizardBackButton"
                text: i18n("Back")
                icon.name: "go-previous"
                enabled: page.controller.stepIndex > 0
                onClicked: page.controller.previousStep()
            }

            QQC2.Button {
                objectName: "wizardNextButton"
                visible: !page.controller.onSummary
                text: i18n("Next")
                icon.name: "go-next"
                enabled: page.controller.canAdvance
                onClicked: page.controller.nextStep()
            }

            QQC2.Button {
                objectName: "wizardCreateButton"
                visible: page.controller.onSummary
                text: page.controller.startAfterCreate ? i18n("Create and start") : i18n("Create")
                icon.name: "list-add"
                enabled: page.controller.canAdvance && !page.operations.busy
                onClicked: page.controller.submit()
            }

            Item {
                Layout.fillWidth: true
            }

            QQC2.Button {
                objectName: "wizardCancelButton"
                text: i18n("Cancel")
                onClicked: page.closeRequested()
            }
        }
    }
}
