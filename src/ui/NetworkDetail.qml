/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Network details (ARCH_V5_V8 §3.2).

    Data comes from `NetworkDetailController` (the existing `/networks` snapshot, no extra request).
    Only two actions exist here: jump to a member container's details, and copy the ID. Network
    create/remove and connect/disconnect land in 6C/6D, which adds an action bar to this same page.
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.kcm.docker as Kontainer

import "components" as Components

Kirigami.Page {
    id: page

    required property string networkId

    readonly property var controller: kcm.controller.networkDetail
    readonly property var network: kcm.controller.networkModel

    signal closeRequested
    /*! Jump to a member container's details (navigation is handled by main.qml). */
    signal containerRequested(string containerId)

    readonly property var operations: kcm.controller.operations
    /*!
     * Whether this is a daemon pre-defined network (`bridge` / `host` / `none`).
     *
     * These cannot be removed: the daemon replies 403 `is a pre-defined network`. The remove entry
     * **never appears** and the page explains why, instead of letting the user fail at the last
     * click (§3.2/§3.3).
     */
    readonly property bool removable: page.controller.valid && !page.controller.predefined
    /*! Whether removal affects connected containers (the confirmation text must say so). */
    readonly property int connectedCount: page.controller.memberCount

    objectName: "networkDetailPage"

    Component.onCompleted: page.controller.setNetworkId(page.networkId)

    actions: [
        Kirigami.Action {
            objectName: "removeNetworkAction"
            text: i18n("Remove network…")
            icon.name: "edit-delete"
            // Hidden/disabled for built-in networks, without write permission, or while busy
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

                /* ---------------- Overview ---------------- */
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

                // Built-in networks cannot be removed: explain why here (daemon replies 403)
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
                    /*
                     * Shrink to content and align left: Kirigami's FormLayout right-aligns the
                     * `[label][field]` group, so at full width the block drifts into the right half
                     * (observed in testing). Fields that need width (long commands) set
                     * Layout.preferredWidth themselves.
                     */
                    Layout.fillWidth: false
                    Layout.alignment: Qt.AlignLeft

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

                /* ---------------- Member containers ---------------- */
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
                        // Only rows with a target (a container id) are clickable
                        enabled: memberRow.target.length > 0
                        onClicked: page.containerRequested(memberRow.target)

                        contentItem: RowLayout {
                            spacing: Kirigami.Units.smallSpacing

                            // Same state icon as Image → Related containers (user asked for consistency)
                            Kirigami.Icon {
                                objectName: "networkMemberStateIcon"
                                // Unknown state: draw nothing rather than a "?" placeholder
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

                /* ---------------- Labels and options ---------------- */
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
        // The consequence text is required: connected containers lose this network
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
