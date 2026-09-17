/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    创建网络对话框（ARCH_V5_V8 §3.3）。

    范围被有意限制：**只创建 bridge 网络**。其它驱动（overlay / macvlan / ipvlan）
    在列表与详情里正常展示，但这里不提供创建入口——overlay 需要 swarm，macvlan/ipvlan
    需要父接口配置，都不是"填个名字"能完成的事（§3.7 明确不做）。

    校验规则只有一份实现（OperationController 转发的 domain 函数）：
    这里做**实时**校验并就地给出原因，提交时控制器会**再校验一次**。
    错误文案由稳定 key 映射而来（C++ 不拼用户可见文本）。
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

import "." as Local

Kirigami.Dialog {
    id: dialog

    /*! `OperationController`（校验、提交、结果都在它身上）。 */
    required property var operations

    signal created(string name)

    objectName: "createNetworkDialog"

    title: i18n("Create network")
    standardButtons: Kirigami.Dialog.NoButton
    preferredWidth: Kirigami.Units.gridUnit * 28

    /*! 错误 key → 文案。 */
    function messageFor(key: string): string {
        switch (key) {
        case "nameRequired":
            return i18n("Enter a name for the network.");
        case "nameInvalid":
            return i18n("Use letters, digits, underscore, dot or dash, and do not start with a separator.");
        case "nameInUse":
            return i18n("A network with this name already exists.");
        case "subnetInvalid":
            return i18n("Enter a subnet in CIDR form, for example 172.30.0.0/16.");
        case "gatewayNeedsSubnet":
            return i18n("A gateway needs a subnet to belong to.");
        case "gatewayInvalid":
            return i18n("Enter a valid gateway address inside the subnet.");
        default:
            return "";
        }
    }

    /*! 当前输入的第一个问题（空 = 可以提交）。 */
    readonly property string currentError: {
        if (nameField.text.trim().length === 0) {
            return ""; // 还没开始填：不提前报错
        }
        const nameError = dialog.operations.networkNameError(nameField.text);
        if (nameError.length > 0) {
            return nameError;
        }
        if (dialog.operations.networkNameTaken(nameField.text)) {
            return "nameInUse";
        }
        const subnetError = dialog.operations.subnetError(subnetField.text);
        if (subnetError.length > 0) {
            return subnetError;
        }
        return dialog.operations.gatewayError(gatewayField.text, subnetField.text);
    }

    readonly property bool canSubmit: {
        if (nameField.text.trim().length === 0) {
            return false;
        }
        return dialog.currentError.length === 0;
    }

    function reset(): void {
        nameField.text = "";
        subnetField.text = "";
        gatewayField.text = "";
        internalCheck.checked = false;
        attachableCheck.checked = false;
        labelsEditor.setEntries([]);
        nameField.forceActiveFocus();
    }

    function submit(): void {
        if (!dialog.canSubmit) {
            return;
        }
        const name = nameField.text.trim();
        // 标签直接来自编辑器（空行与非法键由它自己过滤/报错）
        const labels = labelsEditor.entries();
        if (dialog.operations.createNetwork(name,
                                            subnetField.text.trim(),
                                            gatewayField.text.trim(),
                                            internalCheck.checked,
                                            attachableCheck.checked,
                                            labels)) {
            dialog.created(name);
            dialog.close();
        }
    }

    ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        Kirigami.InlineMessage {
            objectName: "createNetworkError"
            Layout.fillWidth: true
            visible: dialog.currentError.length > 0
            type: Kirigami.MessageType.Error
            text: dialog.messageFor(dialog.currentError)
        }

        QQC2.Label {
            Layout.fillWidth: true
            text: i18n("Only bridge networks can be created here. Use the docker CLI for overlay, macvlan or ipvlan.")
            font: Kirigami.Theme.smallFont
            opacity: 0.8
            wrapMode: Text.WordWrap
        }

        QQC2.TextField {
            id: nameField

            objectName: "networkNameField"
            Layout.fillWidth: true
            placeholderText: i18n("Network name")
            Accessible.name: i18n("Network name")
            onAccepted: dialog.submit()
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            QQC2.TextField {
                id: subnetField

                objectName: "networkSubnetField"
                Layout.fillWidth: true
                placeholderText: i18n("Subnet (optional, for example 172.30.0.0/16)")
                Accessible.name: i18n("Subnet")
            }

            QQC2.TextField {
                id: gatewayField

                objectName: "networkGatewayField"
                Layout.fillWidth: true
                placeholderText: i18n("Gateway (optional)")
                Accessible.name: i18n("Gateway")
                onAccepted: dialog.submit()
            }
        }

        QQC2.Label {
            Layout.fillWidth: true
            text: i18n("Leave the subnet empty to let Docker pick one.")
            font: Kirigami.Theme.smallFont
            opacity: 0.7
            wrapMode: Text.WordWrap
        }

        QQC2.CheckBox {
            id: internalCheck

            objectName: "networkInternalCheck"
            text: i18n("Internal network (no external access)")
        }

        QQC2.CheckBox {
            id: attachableCheck

            objectName: "networkAttachableCheck"
            text: i18n("Allow containers to be attached at runtime")
        }

        QQC2.Label {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.smallSpacing
            text: i18n("Labels")
            font.bold: true
        }

        Local.KeyValueListEditor {
            id: labelsEditor

            objectName: "networkLabelsEditor"
            Layout.fillWidth: true
        }
    }

    customFooterActions: [
        Kirigami.Action {
            objectName: "createNetworkButton"
            text: i18n("Create")
            icon.name: "list-add"
            enabled: dialog.canSubmit
            onTriggered: dialog.submit()
        }
    ]
}
