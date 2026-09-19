/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    创建容器向导（ARCH_V5_V8 §4.4）。

    分步表单，但**状态与校验全在 `CreateContainerController` 里**（C++）：
    七期的校验矩阵有一半要对照后端数据（重名、端口冲突、镜像是否在本地），
    放在 QML 里既测不到、也会和控制器里的规则分叉。这一页只做三件事：
    把输入写进控制器、按 `controller.stepKey` 铺对应的表单、把总览画出来。

    最后一步是**只读总览**：环境变量只列键名（值可能是密码），确认后才提交。
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.kontainer as Kontainer

import "components" as Components

Kirigami.Page {
    id: page

    /*! 从镜像卡片/详情进入时预选的镜像引用。 */
    property string presetImage: ""
    /*! 克隆：用这个容器（id）预填配置。 */
    property string cloneFromContainerId: ""

    readonly property var controller: kcm.controller.createContainer
    readonly property var operations: kcm.controller.operations

    signal closeRequested
    /*! 创建成功：跳到新容器详情页。 */
    signal containerCreated(string containerId)

    objectName: "createContainerPage"

    /*! 预设管理面板是否展开（挂载步骤里）。 */
    property bool presetPanelOpen: false

    /*! 点击步骤按钮被拒绝时的原因 key（空 = 没有）。 */
    property string stepJumpErrorKey: ""

    /*!
     * 特权勾选框的**视图**状态。
     *
     * 不能把 `checked` 直接绑到控制器再在处理器里赋值：那样会把绑定破坏掉，
     * 之后控制器变成 true 也不会再同步（用户实测 F4：确认后框还是空的）。
     */
    property bool privilegedVisual: false

    Connections {
        target: page.controller
        // 控制器是唯一事实来源：它变了就把视图对齐
        function onChanged() {
            page.privilegedVisual = page.controller.privileged;
            /*
             * 同时清掉"跳转被拒绝"的原因。
             *
             * 用户实测：镜像已经选好了，"请选择一个镜像"还一直挂着（只有点标签页才消失）。
             * 原因是那条提示来自 stepJumpErrorKey，而它原来只在**点击步骤按钮成功**时才清；
             * 用"下一步"前进或直接改表单都不会清它。现在表单一有变化就清，
             * 横幅因此回落到当前步骤的真实状态（stepErrorKey）——该消失时立刻消失。
             */
            page.stepJumpErrorKey = "";
        }
    }

    /*!
     * delegate 用的中转对象（八期后的修正）。
     *
     * `Repeater` 的 delegate 里引用**根对象的 id**（page）会抛
     * `ReferenceError: page is not defined`——用户实测的"删不掉端口映射 / 加不了挂载"就是这个：
     * delegate 里的处理器调用 `page.pushPorts()` 直接抛错，改动没写回控制器。
     * 同一文件里**非根**对象的 id 在 delegate 里是可用的，因此这里把 delegate 需要的能力
     * （控制器 + 行同步）集中转发一次。
     */
    QtObject {
        id: wizard

        readonly property var controller: page.controller
        readonly property var operations: page.operations

        /*! 端口/挂载行的编辑一律交给控制器（规则在 C++ 里，delegate 只报"第几行、哪个字段"）。 */
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

    /*! 步骤 key → 标题（顺序由控制器给出，界面不另抄一份）。 */
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

    /*! 校验 key → 文案。 */
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
         * 走到"端口"步骤时刷新一次容器列表。
         *
         * 端口冲突是拿**已发布端口**判断的（`hostPortHolder()`），而容器列表是 5 秒轮询；
         * 用户刚在别处启动了一个占用该端口的容器时，本地列表可能还是旧的——于是"提前拦截"
         * 失效，最后在启动时才收到 `Bind for 0.0.0.0:8100 failed: port is already allocated`。
         */
        function onStepKeyChanged() {
            if (page.controller.stepKey === "ports") {
                kcm.controller.refresh();
            }
        }
    }

    Component.onCompleted: {
        // 网络选择列表是低频数据（进网络页才刷新）：向导自己再保一次险，
        // 否则"没点过网络标签页就选不到网络"（实测反馈 ②）
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

    /* 步骤指示：已完成的步骤可点击回看/修改，未完成的不让跳 */
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
                     * 只读地跟随当前步骤：**不要** checkable。
                     *
                     * checkable 的按钮在点击时 Qt 会先自行翻转 checked；如果 goToStep() 因为
                     * 校验不通过而拒绝跳转，这个翻转不会被纠正——用户看到的就是"点了几下像多选了
                     * 好几步，但页面还停在第一步"（实测反馈 A3）。
                     */
                    checked: page.controller.stepIndex === stepButton.index
                    text: (stepButton.index + 1) + ". " + page.stepTitle(stepButton.modelData)
                    onClicked: {
                        // 被拒绝时要说清为什么：原来只是"没反应"，用户会以为界面坏了
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
            // 当前步骤的问题，以及"点了别的步骤但没跳过去"的原因，都走这一条提示
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

                /* ---------------------------- ① 镜像 ---------------------------- */
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

                    // 可搜索下拉（用户实测 F3：镜像版本多时逐个点选太慢）
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

                /* ---------------------------- ② 基础 ---------------------------- */
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
                            // 名称为空时按镜像生成（见控制器里的说明），因此只要有镜像就能点
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
                        // 下拉里显示的就是会提交的那个网络：不要出现"看着选了 A、实际提交空"。
                        // 网络列表是异步到的，因此 count 变化时也要补一次（创建页面时它可能还是空的）
                        onCountChanged: syncNetworkSelection()
                        onCurrentIndexChanged: syncNetworkSelection()
                        Component.onCompleted: syncNetworkSelection()
                        function syncNetworkSelection(): void {
                            if (networkCombo.count === 0) {
                                return;
                            }
                            // 模型是异步到的：先把下标摆正（此时 currentText 可能还是空的），
                            // 再据它回填控制器——否则会一直停在"看着选了、实际提交空"
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

                /* ---------------------------- ④ 交互 ---------------------------- */
                /* 从"基础"里拆出来（用户实测）：先确定挂载/环境，再决定跑什么命令与工作区 */
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

                    // 用户实测：默认参数下 alpine 的 /bin/sh 读到 EOF 就退出；-i -t 才是常见预期
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


                    // 命令历史（F3）：本地记录 + 已有容器的命令，挑一条直接填进下面的输入框
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

                /* ---------------------------- ⑤ 端口 ---------------------------- */
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

                /* ------------------------ ③ 环境变量与标签 ------------------------ */
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
                        // 值默认按密码显示：环境变量里经常是密钥（四期 §40 的同一约定）
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

                /* ---------------------------- ⑥ 挂载 ---------------------------- */
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: page.controller.stepKey === "mounts"
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Heading {
                        Layout.fillWidth: true
                        level: 3
                        text: i18n("Mounts")
                    }

                    // 预设改成**可搜索下拉**（用户实测：镜像/预设多时逐个点按钮太慢）
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

                    // 手动输入：点一下加一行可编辑的挂载（与预设两条路都可用）
                    QQC2.Button {
                        objectName: "wizardAddMount"
                        text: i18n("Add mount manually")
                        icon.name: "list-add"
                        onClicked: wizard.addMount()
                    }

                }

                /* ---------------------------- ⑦ 资源 ---------------------------- */
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
                            // 0 = 不限制；用 0.5 这样的值需要文本输入
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
                         * 这里**刻意不用 CheckBox 自己的勾选机制**（checkable: false）。
                         *
                         * 用户实测反馈 F4：确认通过后勾选框仍是空的。根因是 CheckBox 点击时会直接给
                         * `checked` 赋值，这会破坏 `checked: …` 这条绑定，之后数据变了也不再同步。
                         * 关掉 checkable 后，点击只是"切换意图"，勾选态完全由 `privilegedVisual`
                         * （进而由控制器）决定，绑定永远不会被破坏。
                         */
                        checkable: false
                        checked: page.privilegedVisual
                        text: i18n("Run with extended privileges (--privileged)")
                        onClicked: {
                            if (page.controller.privileged) {
                                // 已启用：直接关掉（可逆，不需要确认）
                                page.controller.privileged = false;
                            } else {
                                // 未启用：强确认（要把容器名原样输入一遍）
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

                /* ---------------------------- ⑧ 总览 ---------------------------- */
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

    /* 端口的行模型：QML 里改一行要回写整个列表（控制器持有状态），
       因此用一个 ListModel 做"编辑缓冲"，字段改动立刻回写控制器。 */
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

    /*! 把控制器的行数据同步进编辑缓冲（只在内容不同时重建，避免打断正在输入的行）。 */
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

    /*! 编辑缓冲与控制器是否一致（不一致才重建，避免打断正在输入的那一行）。 */
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
        // 强确认：把容器名原样输入一遍（用户确认过的方案；--privileged 不需要也不能靠 polkit 提权）
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
