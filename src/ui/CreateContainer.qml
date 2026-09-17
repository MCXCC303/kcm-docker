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

    /*! 步骤 key → 标题（顺序由控制器给出，界面不另抄一份）。 */
    function stepTitle(key: string): string {
        switch (key) {
        case "image":
            return i18n("Image");
        case "basics":
            return i18n("Basics");
        case "ports":
            return i18n("Ports");
        case "environment":
            return i18n("Environment & labels");
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

    Component.onCompleted: {
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
                    checked: page.controller.stepIndex === stepButton.index
                    checkable: true
                    text: (stepButton.index + 1) + ". " + page.stepTitle(stepButton.modelData)
                    onClicked: page.controller.goToStep(stepButton.modelData)
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
            visible: page.controller.stepErrorKey.length > 0
            type: Kirigami.MessageType.Error
            text: page.errorText(page.controller.stepErrorKey)
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
                        visible: imageRepeater.count > 0
                    }

                    Repeater {
                        id: imageRepeater

                        model: page.controller.availableImages

                        delegate: QQC2.RadioButton {
                            id: imageOption

                            required property var modelData

                            objectName: "wizardImageOption"
                            Layout.fillWidth: true
                            checked: page.controller.image === imageOption.modelData.reference
                            text: imageOption.modelData.reference
                            onClicked: page.controller.image = imageOption.modelData.reference
                        }
                    }

                    Components.EmptyPlaceholder {
                        objectName: "wizardNoLocalImages"
                        Layout.fillWidth: true
                        message: imageRepeater.count === 0 ? i18n("No local images. Pull one from the Images tab first.") : ""
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
                            enabled: page.controller.suggestedName().length > 0
                            onClicked: page.controller.name = page.controller.suggestedName()
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

                /* ---------------------------- ③ 端口 ---------------------------- */
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: page.controller.stepKey === "ports"
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Heading {
                        Layout.fillWidth: true
                        level: 3
                        text: i18n("Published ports")
                    }

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("Leave the host port empty to let Docker pick a free one.")
                        font: Kirigami.Theme.smallFont
                        opacity: 0.75
                    }

                    Repeater {
                        id: portRepeater

                        model: portRowsModel

                        delegate: RowLayout {
                            id: portRow

                            required property int index
                            required property int containerPort
                            required property int hostPort
                            required property string hostIp
                            required property string protocol

                            objectName: "wizardPortRow"
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            QQC2.SpinBox {
                                objectName: "wizardContainerPort"
                                from: 1
                                to: 65535
                                value: portRow.containerPort
                                onValueModified: {
                                    portRowsModel.setProperty(portRow.index, "containerPort", value);
                                    page.pushPorts();
                                }
                            }
                            QQC2.Label {
                                text: "→"
                            }
                            QQC2.SpinBox {
                                objectName: "wizardHostPort"
                                from: 0
                                to: 65535
                                value: portRow.hostPort
                                onValueModified: {
                                    portRowsModel.setProperty(portRow.index, "hostPort", value);
                                    page.pushPorts();
                                }
                            }
                            QQC2.ComboBox {
                                objectName: "wizardPortProtocol"
                                textRole: "text"
                                valueRole: "value"
                                model: [
                                    {text: i18n("TCP"), value: "tcp"},
                                    {text: i18n("UDP"), value: "udp"}
                                ]
                                Component.onCompleted: currentIndex = indexOfValue(portRow.protocol)
                                onActivated: {
                                    portRowsModel.setProperty(portRow.index, "protocol", currentValue);
                                    page.pushPorts();
                                }
                            }
                            QQC2.Button {
                                objectName: "wizardRemovePort"
                                icon.name: "list-remove"
                                flat: true
                                Accessible.name: i18n("Remove this port")
                                onClicked: {
                                    portRowsModel.remove(portRow.index);
                                    page.pushPorts();
                                }
                            }
                        }
                    }

                    QQC2.Button {
                        objectName: "wizardAddPort"
                        text: i18n("Add port")
                        icon.name: "list-add"
                        onClicked: {
                            const rows = page.controller.portRows.slice();
                            rows.push({containerPort: 80, hostPort: 0, hostIp: "", protocol: "tcp"});
                            page.controller.portRows = rows;
                        }
                    }

                    Components.EmptyPlaceholder {
                        objectName: "wizardNoPorts"
                        Layout.fillWidth: true
                        message: portRepeater.count === 0 ? i18n("No published ports. The container will only be reachable on its networks.") : ""
                    }
                }

                /* ------------------------ ④ 环境变量与标签 ------------------------ */
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

                /* ---------------------------- ⑤ 挂载 ---------------------------- */
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: page.controller.stepKey === "mounts"
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Heading {
                        Layout.fillWidth: true
                        level: 3
                        text: i18n("Mounts")
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        QQC2.Label {
                            Layout.fillWidth: true
                            visible: presetRepeater.count > 0
                            text: i18n("Quick add from your presets:")
                            font.bold: true
                        }

                        QQC2.Button {
                            objectName: "wizardManagePresetsButton"
                            text: page.controller.presets.length > 0 ? i18n("Manage presets…") : i18n("Add a preset…")
                            icon.name: "configure"
                            onClicked: page.presetPanelOpen = !page.presetPanelOpen
                        }
                    }

                    Flow {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        Repeater {
                            id: presetRepeater

                            model: page.controller.presets

                            delegate: QQC2.Button {
                                id: presetButton

                                required property var modelData

                                objectName: "wizardPresetButton"
                                text: (presetButton.modelData.favorite ? "★ " : "") + presetButton.modelData.source
                                    + " → " + presetButton.modelData.destination
                                onClicked: page.controller.addMountFromPreset(presetButton.modelData.id)
                            }
                        }
                    }

                    Kirigami.Separator {
                        Layout.fillWidth: true
                        visible: page.presetPanelOpen
                    }

                    ColumnLayout {
                        objectName: "wizardPresetPanel"
                        Layout.fillWidth: true
                        visible: page.presetPanelOpen
                        spacing: Kirigami.Units.smallSpacing

                        QQC2.Label {
                            Layout.fillWidth: true
                            text: i18n("Presets are stored in your own configuration (~/.config/kontainerrc).")
                            font: Kirigami.Theme.smallFont
                            opacity: 0.75
                            wrapMode: Text.WordWrap
                        }

                        Repeater {
                            id: presetManageRepeater

                            model: page.controller.presets

                            delegate: RowLayout {
                                id: presetRow

                                required property var modelData

                                objectName: "wizardPresetManageRow"
                                Layout.fillWidth: true
                                spacing: Kirigami.Units.smallSpacing

                                QQC2.TextField {
                                    objectName: "wizardPresetSourceField"
                                    Layout.fillWidth: true
                                    text: presetRow.modelData.source
                                    onEditingFinished: kcm.controller.mountPresets.update(presetRow.modelData.id, text,
                                                                                         presetRow.modelData.destination,
                                                                                         presetRow.modelData.readOnly,
                                                                                         presetRow.modelData.note)
                                }
                                QQC2.TextField {
                                    objectName: "wizardPresetDestinationField"
                                    Layout.fillWidth: true
                                    text: presetRow.modelData.destination
                                    onEditingFinished: kcm.controller.mountPresets.update(presetRow.modelData.id,
                                                                                         presetRow.modelData.source,
                                                                                         text,
                                                                                         presetRow.modelData.readOnly,
                                                                                         presetRow.modelData.note)
                                }
                                QQC2.CheckBox {
                                    objectName: "wizardPresetFavoriteCheck"
                                    text: i18n("Favourite")
                                    checked: presetRow.modelData.favorite
                                    onToggled: kcm.controller.mountPresets.setFavorite(presetRow.modelData.id, checked)
                                }
                                QQC2.Button {
                                    objectName: "wizardPresetMoveUp"
                                    icon.name: "go-up"
                                    flat: true
                                    Accessible.name: i18n("Move up")
                                    onClicked: kcm.controller.mountPresets.moveUp(presetRow.modelData.id)
                                }
                                QQC2.Button {
                                    objectName: "wizardPresetMoveDown"
                                    icon.name: "go-down"
                                    flat: true
                                    Accessible.name: i18n("Move down")
                                    onClicked: kcm.controller.mountPresets.moveDown(presetRow.modelData.id)
                                }
                                QQC2.Button {
                                    objectName: "wizardPresetDelete"
                                    icon.name: "edit-delete"
                                    flat: true
                                    Accessible.name: i18n("Remove this preset")
                                    onClicked: kcm.controller.mountPresets.remove(presetRow.modelData.id)
                                }
                            }
                        }

                        // 新增：宿主路径 + 容器路径（类型默认 bind；命名卷用类型下拉在挂载行里选）
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            QQC2.TextField {
                                id: newPresetSource

                                objectName: "wizardNewPresetSource"
                                Layout.fillWidth: true
                                placeholderText: i18n("Host path or volume name")
                                Accessible.name: i18n("Preset source")
                            }
                            QQC2.TextField {
                                id: newPresetDestination

                                objectName: "wizardNewPresetDestination"
                                Layout.fillWidth: true
                                placeholderText: i18n("Container path")
                                Accessible.name: i18n("Preset destination")
                            }
                            QQC2.Button {
                                objectName: "wizardNewPresetAdd"
                                text: i18n("Add")
                                icon.name: "list-add"
                                enabled: newPresetSource.text.length > 0 && newPresetDestination.text.length > 0
                                onClicked: {
                                    const id = kcm.controller.mountPresets.add(newPresetSource.text, newPresetDestination.text,
                                                                               "bind", false, "");
                                    if (id.length > 0) {
                                        newPresetSource.text = "";
                                        newPresetDestination.text = "";
                                    }
                                }
                            }
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
                                onActivated: {
                                    mountRowsModel.setProperty(mountRow.index, "type", currentValue);
                                    page.pushMounts();
                                }
                            }
                            QQC2.TextField {
                                objectName: "wizardMountSource"
                                Layout.fillWidth: true
                                enabled: mountRow.type !== "tmpfs"
                                placeholderText: mountRow.type === "volume" ? i18n("Volume name") : i18n("Host path")
                                Accessible.name: i18n("Mount source")
                                text: mountRow.source
                                onTextChanged: {
                                    mountRowsModel.setProperty(mountRow.index, "source", text);
                                    page.pushMounts();
                                }
                            }
                            QQC2.TextField {
                                objectName: "wizardMountDestination"
                                Layout.fillWidth: true
                                placeholderText: i18n("Container path")
                                Accessible.name: i18n("Container path")
                                text: mountRow.destination
                                onTextChanged: {
                                    mountRowsModel.setProperty(mountRow.index, "destination", text);
                                    page.pushMounts();
                                }
                            }
                            QQC2.CheckBox {
                                objectName: "wizardMountReadOnly"
                                text: i18n("Read-only")
                                checked: mountRow.readOnly
                                onToggled: {
                                    mountRowsModel.setProperty(mountRow.index, "readOnly", checked);
                                    page.pushMounts();
                                }
                            }
                            QQC2.Button {
                                objectName: "wizardRemoveMount"
                                icon.name: "list-remove"
                                flat: true
                                Accessible.name: i18n("Remove this mount")
                                onClicked: page.controller.removeMountAt(mountRow.index)
                            }
                        }
                    }

                    QQC2.Button {
                        objectName: "wizardAddMount"
                        text: i18n("Add mount")
                        icon.name: "list-add"
                        onClicked: page.controller.addEmptyMount()
                    }

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("A host path that does not exist yet is fine: Docker creates the directory.")
                        font: Kirigami.Theme.smallFont
                        opacity: 0.75
                    }
                }

                /* ---------------------------- ⑥ 资源 ---------------------------- */
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
                        checked: page.controller.privileged
                        text: i18n("Run with extended privileges (--privileged)")
                        // 勾选时必须二次确认：这是整套表单里唯一能直接拿到宿主 root 的开关
                        onToggled: {
                            if (checked) {
                                privilegedCheck.checked = false;
                                privilegedDialog.open();
                            } else {
                                page.controller.privileged = false;
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

                /* ---------------------------- ⑦ 总览 ---------------------------- */
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

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("Environment variable values are not shown here on purpose; only their names.")
                        font: Kirigami.Theme.smallFont
                        opacity: 0.7
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

    /*! 把端口编辑缓冲回写成控制器的行数据（控制器持有真正的状态）。 */
    function pushPorts(): void {
        const rows = [];
        for (let i = 0; i < portRowsModel.count; ++i) {
            const item = portRowsModel.get(i);
            rows.push({
                containerPort: item.containerPort,
                hostPort: item.hostPort,
                hostIp: item.hostIp,
                protocol: item.protocol
            });
        }
        page.controller.portRows = rows;
    }

    function pushMounts(): void {
        const rows = [];
        for (let i = 0; i < mountRowsModel.count; ++i) {
            const item = mountRowsModel.get(i);
            rows.push({
                type: item.type,
                source: item.source,
                destination: item.destination,
                readOnly: item.readOnly
            });
        }
        page.controller.mountRows = rows;
    }

    function rowsEqual(model, rows): bool {
        if (model.count !== rows.length) {
            return false;
        }
        for (let i = 0; i < model.count; ++i) {
            const item = model.get(i);
            const row = rows[i];
            if (String(item.containerPort ?? "") !== String(row.containerPort ?? "")
                || String(item.hostPort ?? "") !== String(row.hostPort ?? "")
                || String(item.source ?? "") !== String(row.source ?? "")
                || String(item.destination ?? "") !== String(row.destination ?? "")) {
                return false;
            }
        }
        return true;
    }

    Components.ConfirmDialog {
        id: privilegedDialog

        objectName: "wizardPrivilegedDialog"
        headingText: i18n("Run with extended privileges")
        questionText: i18n("Give this container the same access as root on the host?")
        consequenceText: i18n("The container can access all devices and host files. Only continue if you trust the image.")
        acceptText: i18n("Yes, run privileged")
        destructive: true
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
