/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    数据卷详情（ARCH_V5_V8 §3.5）。

    挂载点在这里比列表里更有用：可以复制、也可以在系统文件管理器中打开
    （复用四期的宿主路径服务——这是唯一的非 Docker 外部动作）。
    删除走确认对话框，且**不提供 force**：被容器使用时让引擎拒绝并说明原因。
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.kontainer as Kontainer

import "components" as Components

Kirigami.Page {
    id: page

    required property string volumeName

    readonly property var controller: kcm.controller.volumeDetail
    readonly property var operations: kcm.controller.operations

    signal closeRequested

    objectName: "volumeDetailPage"

    Component.onCompleted: page.controller.selectVolume(page.volumeName)

    actions: [
        Kirigami.Action {
            objectName: "removeVolumeAction"
            text: i18n("Remove volume…")
            icon.name: "edit-delete"
            // 只读模式不出现；有操作在途时不可用
            visible: page.controller.valid && page.operations.writeAllowed
            enabled: !page.operations.isTargetBusy("volume:" + page.volumeName)
            onTriggered: removeVolumeDialog.open()
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
            objectName: "volumeGoneMessage"
            Layout.fillWidth: true
            visible: !page.controller.valid
            type: Kirigami.MessageType.Information
            text: i18n("This volume no longer exists.")
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
                    text: page.controller.name.length > 0 ? page.controller.name : i18n("Volume")
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    Components.FieldChip {
                        objectName: "volumeDetailDriverChip"
                        muted: true
                        text: page.controller.driver
                    }
                    Components.FieldChip {
                        objectName: "volumeDetailScopeChip"
                        muted: true
                        text: page.controller.scope
                    }
                    Components.FieldChip {
                        objectName: "volumeDetailInUseChip"
                        visible: page.controller.usageKnown && page.controller.inUse
                        muted: true
                        text: i18np("used by %1 container", "used by %1 containers", page.controller.refCount)
                    }
                    Components.FieldChip {
                        objectName: "volumeDetailUnknownChip"
                        visible: !page.controller.usageKnown
                        muted: true
                        text: i18n("usage unknown")
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                }

                /* 挂载点：可复制，也可以在文件管理器中打开 */
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    Components.CopyableText {
                        objectName: "volumeMountpointValue"
                        Layout.fillWidth: true
                        value: page.controller.mountpoint
                        fieldLabel: i18n("mount point")
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        QQC2.Button {
                            objectName: "openVolumeFolderButton"
                            text: i18n("Open host folder")
                            icon.name: "folder-open"
                            enabled: page.controller.mountpointUsable
                            // 打开宿主目录是四期的宿主路径服务（唯一的非 Docker 外部动作）
                            onClicked: kcm.controller.openHostPath(page.controller.mountpoint)
                        }

                        QQC2.Label {
                            Layout.fillWidth: true
                            text: kcm.controller.hostPathStateKey(page.controller.mountpoint) === "missing"
                                ? i18n("The host path does not exist.")
                                : ""
                            font: Kirigami.Theme.smallFont
                            opacity: 0.75
                            elide: Text.ElideRight
                        }
                    }
                }

                Kirigami.FormLayout {
                    Layout.fillWidth: true

                    QQC2.Label {
                        objectName: "volumeSizeValue"
                        Kirigami.FormData.label: i18n("Size:")
                        text: page.controller.sizeKnown ? Kontainer.Format.byteSize(page.controller.sizeBytes) : i18n("—")
                    }
                    QQC2.Label {
                        objectName: "volumeRefCountValue"
                        Kirigami.FormData.label: i18n("Used by:")
                        text: page.controller.usageKnown
                            ? i18ncp("@info volume reference count", "%1 container", "%1 containers", page.controller.refCount)
                            : i18n("—")
                    }
                    QQC2.Label {
                        objectName: "volumeCreatedValue"
                        Kirigami.FormData.label: i18n("Created:")
                        text: page.controller.created.toLocaleString(Qt.locale(), Locale.ShortFormat)
                    }
                    QQC2.Label {
                        objectName: "volumeStatusValue"
                        visible: page.controller.status.length > 0
                        Kirigami.FormData.label: i18n("Status:")
                        text: page.controller.status
                    }
                }

                Components.CollapsibleSection {
                    objectName: "volumeLabelsSection"
                    Layout.fillWidth: true
                    title: i18n("Labels")
                    expanded: false
                    visible: !page.controller.labels.empty

                    Components.KeyValueList {
                        model: page.controller.labels
                    }
                }

                Components.CollapsibleSection {
                    objectName: "volumeOptionsSection"
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
        id: removeVolumeDialog

        objectName: "removeVolumeDialog"
        headingText: i18n("Remove volume")
        questionText: i18n("Remove the volume “%1”?", page.controller.name)
        // 后果说明是必填：卷里的数据会一起消失
        consequenceText: page.controller.usageKnown && page.controller.inUse
            ? i18n("The volume and the data inside it are deleted. Containers that use it must be removed or disconnected first.")
            : i18n("The volume and the data inside it are deleted. This cannot be undone.")
        acceptText: i18n("Remove")
        destructive: true
        onConfirmed: {
            page.operations.removeVolume(page.volumeName);
            page.closeRequested();
        }
    }
}
