/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    网络详情（ARCH_V5_V8 §3.2）。

    数据来自 `NetworkDetailController`（它是后端已拿到的 `/networks` 快照，不再单独发请求）。
    页面上能做的动作只有两类：跳到某个成员容器的详情、复制 ID——网络本身的创建/删除
    与连接/断开在 6C/6D 落地（那时会加动作条，保持在同一个页面）。
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.kontainer as Kontainer

import "components" as Components

Kirigami.Page {
    id: page

    required property string networkId

    readonly property var controller: kcm.controller.networkDetail
    readonly property var network: kcm.controller.networkModel

    signal closeRequested
    /*! 跳到成员容器的详情（由 main.qml 负责导航）。 */
    signal containerRequested(string containerId)

    readonly property var operations: kcm.controller.operations
    /*!
     * 这是不是 daemon 预定义网络（`bridge` / `host` / `none`）。
     *
     * 预定义网络删不掉：daemon 会回 403 `is a pre-defined network`。界面上**不出现**
     * 删除入口，并在页面里说明原因——不让用户点到最后才失败（§3.2/§3.3）。
     */
    readonly property bool removable: page.controller.valid && !page.controller.predefined
    /*! 删除会不会影响已连接的容器（确认文案要写清楚）。 */
    readonly property int connectedCount: page.controller.memberCount

    objectName: "networkDetailPage"

    Component.onCompleted: page.controller.setNetworkId(page.networkId)

    actions: [
        Kirigami.Action {
            objectName: "removeNetworkAction"
            text: i18n("Remove network…")
            icon.name: "edit-delete"
            // 内置网络、没有写权限、或有操作在途时不出现/不可用
            visible: page.removable && page.operations.writeAllowed
            enabled: !page.operations.isTargetBusy("network:" + page.networkId)
            onTriggered: removeNetworkDialog.open()
        },
        Kirigami.Action {
            text: i18n("Back")
            icon.name: "go-previous"
            onTriggered: page.closeRequested()
        }
    ]

    ColumnLayout {
        anchors.fill: parent
        spacing: Kirigami.Units.smallSpacing

        Kirigami.InlineMessage {
            objectName: "networkGoneMessage"
            Layout.fillWidth: true
            visible: !page.controller.valid
            type: Kirigami.MessageType.Information
            text: i18n("This network no longer exists.")
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

                Kirigami.Heading {
                    Layout.fillWidth: true
                    Layout.topMargin: Kirigami.Units.smallSpacing
                    level: 2
                    text: page.controller.name.length > 0 ? page.controller.name : i18n("Network")
                }

                /* ---------------- 概览 ---------------- */
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    Components.FieldChip {
                        objectName: "networkDetailDriverChip"
                        muted: true
                        text: page.controller.driver
                    }
                    Components.FieldChip {
                        objectName: "networkDetailScopeChip"
                        muted: true
                        text: page.controller.scope
                    }
                    Components.FieldChip {
                        objectName: "networkDetailPredefinedChip"
                        visible: page.controller.predefined
                        muted: true
                        text: i18n("built-in")
                    }
                    Components.FieldChip {
                        objectName: "networkDetailInternalChip"
                        visible: page.controller.internal
                        muted: true
                        text: i18n("internal")
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                }

                // 内置网络删不掉：在详情页把原因写清楚（daemon 会回 403）
                Kirigami.InlineMessage {
                    objectName: "networkPredefinedNotice"
                    Layout.fillWidth: true
                    visible: page.controller.predefined
                    type: Kirigami.MessageType.Information
                    text: i18n("This network is built into Docker and cannot be removed.")
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    Components.CopyableText {
                        objectName: "networkDetailId"
                        Layout.fillWidth: true
                        value: page.controller.shortId
                        copyValue: page.networkId
                        fieldLabel: i18n("network ID")
                    }
                }

                Kirigami.FormLayout {
                    Layout.fillWidth: true

                    QQC2.Label {
                        objectName: "networkDetailSubnetLabel"
                        Kirigami.FormData.label: i18n("Subnet:")
                        text: page.controller.subnet.length > 0 ? page.controller.subnet : i18n("—")
                        font.family: "monospace"
                    }
                    QQC2.Label {
                        objectName: "networkDetailGatewayLabel"
                        Kirigami.FormData.label: i18n("Gateway:")
                        text: page.controller.gateway.length > 0 ? page.controller.gateway : i18n("—")
                        font.family: "monospace"
                    }
                    QQC2.Label {
                        objectName: "networkDetailCreatedLabel"
                        Kirigami.FormData.label: i18n("Created:")
                        text: page.controller.created.toLocaleString(Qt.locale(), Locale.ShortFormat)
                    }
                    QQC2.Label {
                        objectName: "networkDetailAttachableLabel"
                        Kirigami.FormData.label: i18n("Attachable:")
                        text: page.controller.attachable ? i18n("Yes") : i18n("No")
                    }
                }

                /* ---------------- 成员容器 ---------------- */
                Kirigami.Heading {
                    Layout.fillWidth: true
                    level: 3
                    text: i18nc("@title:section network members", "Connected containers")
                }

                Components.EmptyPlaceholder {
                    objectName: "networkMembersEmptyPlaceholder"
                    Layout.fillWidth: true
                    message: page.controller.members.empty ? i18n("No container is connected to this network.") : ""
                }

                Repeater {
                    model: page.controller.members

                    delegate: QQC2.ItemDelegate {
                        id: memberRow

                        required property string label
                        required property string value
                        required property string detail
                        required property string entryKey
                        required property string stateKey
                        required property string target

                        objectName: "networkMemberRow"
                        Layout.fillWidth: true
                        // 可跳转的行才响应点击（target 是容器 id）
                        enabled: memberRow.target.length > 0
                        onClicked: page.containerRequested(memberRow.target)

                        contentItem: RowLayout {
                            spacing: Kirigami.Units.smallSpacing

                            // 与「镜像 → 关联容器」同一种状态图标（用户反馈要统一）
                            Kirigami.Icon {
                                objectName: "networkMemberStateIcon"
                                // 状态未知时干脆不画：宁可少一个图标，也不要一个"?"占位
                                visible: memberRow.stateKey.length > 0
                                source: Kontainer.Presentation.stateIconName(memberRow.stateKey)
                                color: Components.StatusPalette.color(Kontainer.Presentation.stateSemanticKey(memberRow.stateKey, "none"))
                                implicitWidth: Kirigami.Units.iconSizes.small
                                implicitHeight: Kirigami.Units.iconSizes.small
                            }
                            QQC2.Label {
                                Layout.fillWidth: true
                                text: memberRow.label
                                elide: Text.ElideMiddle
                            }
                            QQC2.Label {
                                objectName: "networkMemberStateText"
                                visible: memberRow.stateKey.length > 0
                                text: Kontainer.Presentation.stateText(memberRow.stateKey)
                                font: Kirigami.Theme.smallFont
                                opacity: 0.8
                            }
                            QQC2.Label {
                                objectName: "networkMemberAddress"
                                visible: memberRow.value.length > 0
                                text: memberRow.value
                                font.family: "monospace"
                                opacity: 0.8
                            }
                            QQC2.Label {
                                visible: memberRow.detail.length > 0
                                text: memberRow.detail
                                font.family: "monospace"
                                opacity: 0.6
                            }
                            Kirigami.Icon {
                                source: "go-next-symbolic"
                                implicitWidth: Kirigami.Units.iconSizes.small
                                implicitHeight: Kirigami.Units.iconSizes.small
                                opacity: 0.6
                            }
                        }
                    }
                }

                /* ---------------- 标签与选项 ---------------- */
                Components.CollapsibleSection {
                    objectName: "networkLabelsSection"
                    Layout.fillWidth: true
                    title: i18n("Labels")
                    expanded: false
                    visible: !page.controller.labels.empty

                    Components.KeyValueList {
                        model: page.controller.labels
                    }
                }

                Components.CollapsibleSection {
                    objectName: "networkOptionsSection"
                    Layout.fillWidth: true
                    title: i18n("Driver options")
                    expanded: false
                    visible: !page.controller.options.empty

                    Components.KeyValueList {
                        model: page.controller.options
                    }
                }

                Item {
                    Layout.fillHeight: true
                }
            }
        }
    }

    Components.ConfirmDialog {
        id: removeNetworkDialog

        objectName: "removeNetworkDialog"
        headingText: i18n("Remove network")
        questionText: i18n("Remove the network “%1”?", page.controller.name)
        // 后果说明是必填：连着的容器会失去这个网络（这正是用户需要知道的）
        consequenceText: page.connectedCount > 0
            ? i18ncp("@info network removal consequence", "One connected container loses this network.", "%1 connected containers lose this network.", page.connectedCount)
            : i18n("No container is connected to this network.")
        acceptText: i18n("Remove")
        destructive: true
        onConfirmed: {
            page.operations.removeNetwork(page.networkId, page.controller.name);
            page.closeRequested();
        }
    }
}
