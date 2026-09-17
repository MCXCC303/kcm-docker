/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    把容器连接到网络（ARCH_V5_V8 §3.4）。

    只列**还没连上**的网络（已连接的禁用并标注），因为"再连一次"不是用户想做的事；
    别名（aliases）让同一网络里的其它容器用名字互相访问，比 IP 稳定，因此值得一个输入框。
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

Kirigami.Dialog {
    id: dialog

    /*! `OperationController`（连接与结果都在它身上）。 */
    required property var operations
    /*! 要连接的容器 Id。 */
    required property string containerId
    /*!
     * 全部网络：`NetworkModel::summaries()` 的普通数组（`{id, name, driver, predefined}`）。
     *
     * 刻意不用模型对象：Kirigami.Dialog 的内容在弹层里有自己的实例树，
     * 递一个普通数组能保证"渲染的那一份"与"我们查询的那一份"数据一致（实测踩过）。
     */
    required property var networks
    /*! 容器**已经**连接的网络名（这些不再提供连接入口）。 */
    required property var connectedNames

    signal connected(string networkId)

    objectName: "connectNetworkDialog"

    title: i18n("Connect to a network")
    standardButtons: Kirigami.Dialog.NoButton
    preferredWidth: Kirigami.Units.gridUnit * 28

    /*! 当前选中的网络 Id（空 = 还没选）。 */
    property string selectedNetworkId: ""
    /*! 最近一次失败的说明（控制器给的用户文案）。 */
    property string errorText: ""

    /*!
     * 这个网络能不能连（已经连上的当然不能再连一次）。
     *
     * 单独抽成一个函数：delegate 的 `enabled`、可选数量与界面文案都用它，
     * 也就不会有"标注说已连接、却还能点"这种自相矛盾。
     */
    function isConnectable(networkName: string): bool {
        return dialog.connectedNames.indexOf(networkName) < 0;
    }

    /*! 还**没连上**的网络数（0 = 没有可连的网络，界面据此给出说明）。 */
    readonly property int availableCount: {
        let count = 0;
        for (let i = 0; i < dialog.networks.length; ++i) {
            if (dialog.isConnectable(dialog.networks[i].name)) {
                ++count;
            }
        }
        return count;
    }

    function reset(): void {
        dialog.selectedNetworkId = "";
        dialog.errorText = "";
        aliasesField.text = "";
    }

    function submit(): void {
        if (dialog.selectedNetworkId.length === 0) {
            return;
        }
        dialog.errorText = "";
        if (dialog.operations.connectContainerToNetwork(dialog.selectedNetworkId, dialog.containerId, aliasesField.text)) {
            dialog.connected(dialog.selectedNetworkId);
            dialog.close();
            return;
        }
        // 失败原因（写权限、目标忙碌等）由控制器给出用户文案
        dialog.errorText = dialog.operations.resultText;
    }

    ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        Kirigami.InlineMessage {
            objectName: "connectNetworkError"
            Layout.fillWidth: true
            visible: dialog.errorText.length > 0
            type: Kirigami.MessageType.Error
            text: dialog.errorText
        }

        Kirigami.InlineMessage {
            objectName: "connectNetworkEmptyMessage"
            Layout.fillWidth: true
            visible: dialog.availableCount === 0
            type: Kirigami.MessageType.Information
            text: i18n("This container is already connected to every network. Create another network first.")
        }

        QQC2.Label {
            Layout.fillWidth: true
            visible: dialog.availableCount > 0
            text: i18n("Choose a network:")
            font.bold: true
        }

        /* 用 ListView 而不是 Repeater：Kirigami.Dialog 的内容在弹层里有独立的实例树，
           Repeater 建出来的条目未必挂在"被渲染的那一份"上（实测：Repeater 里 count=4，
           屏幕上却是空的）。ListView 与主页面里那份网络列表用的是同一条已知可行的路径。 */
        ListView {
            id: networkList

            objectName: "connectNetworkList"
            Layout.fillWidth: true
            // 高度由**数据条数**算出，不能等 contentHeight：ListView 只有可见区域才建
            // delegate，高度为 0 时 contentHeight 永远是 0（实测踩过这个死循环）
            Layout.preferredHeight: Math.min(dialog.networks.length * Kirigami.Units.gridUnit * 1.9,
                                             Kirigami.Units.gridUnit * 12)
            visible: dialog.availableCount > 0
            model: dialog.networks
            clip: true
            spacing: 0

            delegate: QQC2.RadioButton {
                id: networkOption

                objectName: "connectNetworkOption"
                width: networkList.width
                // 已经连上的网络：标注出来但不可再选（避免"再连一次"这种无意义操作）
                required property var modelData

                enabled: dialog.isConnectable(networkOption.modelData.name)
                text: networkOption.modelData.name + " · " + networkOption.modelData.driver
                    + (networkOption.enabled ? "" : " — " + i18n("already connected"))
                onClicked: dialog.selectedNetworkId = networkOption.modelData.id
            }
        }

        QQC2.TextField {
            id: aliasesField

            objectName: "connectNetworkAliasesField"
            Layout.fillWidth: true
            visible: dialog.availableCount > 0
            placeholderText: i18n("Aliases (optional, comma separated)")
            Accessible.name: i18n("Aliases")
        }

        QQC2.Label {
            Layout.fillWidth: true
            visible: dialog.availableCount > 0
            text: i18n("Other containers on the same network can reach this one by its aliases.")
            font: Kirigami.Theme.smallFont
            opacity: 0.75
            wrapMode: Text.WordWrap
        }
    }

    customFooterActions: [
        Kirigami.Action {
            objectName: "connectNetworkButton"
            text: i18n("Connect")
            icon.name: "network-connect"
            enabled: dialog.selectedNetworkId.length > 0
            onTriggered: dialog.submit()
        }
    ]
}
