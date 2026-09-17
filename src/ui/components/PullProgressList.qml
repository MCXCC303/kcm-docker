/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    拉取列表（ARCH_V4 §2.4）。

    拉取是长任务：可以同时进行多个不同镜像，关掉拉取对话框也不会中断。
    因此进度不放在模态对话框里，而是放在这里——

      - 进行中：进度条（总量未知时是不确定态）+ 引擎状态原文 + 层数 + 取消
      - 已结束：成功 / 已取消 / 失败；**失败会带着引擎原文一直留着**，
        直到用户点掉它，这样「拉取失败」不会被静默吞掉
      - 多个已完成记录可以一键清空

    用法：

        Components.PullProgressList {
            Layout.fillWidth: true
            operations: root.operations
        }
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

import "." as Local

ColumnLayout {
    id: root

    required property var operations

    /*! 凭据相关的失败（引擎回 401/403）：引导用户去登录，而不是反复重试。 */
    signal loginRequested(string reference)

    objectName: "pullProgressList"
    visible: root.operations.pulls.count > 0
    spacing: Kirigami.Units.smallSpacing

    Kirigami.Heading {
        Layout.fillWidth: true
        level: 3
        text: root.operations.pulls.activeCount > 0
            ? i18n("Pulling images (%1)", root.operations.pulls.activeCount)
            : i18n("Recent pulls")
    }

    // 「清空已结束」只在确实有已结束记录时出现
    QQC2.Button {
        objectName: "clearFinishedPullsButton"
        Layout.alignment: Qt.AlignRight
        visible: root.operations.pulls.finishedCount > 0
        text: i18n("Clear finished")
        icon.name: "edit-clear-all"
        flat: true
        onClicked: root.operations.clearFinishedPulls()
    }

    Repeater {
        model: root.operations.pulls

        delegate: Kirigami.AbstractCard {
            id: pullCard

            required property string reference
            required property string statusKey
            required property string statusText
            required property string detailText
            required property double progress
            required property bool progressKnown
            required property int completedLayers
            required property int totalLayers
            required property bool active
            /*! 失败种类 key（`permissionDenied` 时给「去登录…」）。 */
            required property string errorKindKey

            objectName: "pullEntry"
            Layout.fillWidth: true
            showClickFeedback: false

            readonly property bool failed: pullCard.statusKey === "failed"
            readonly property bool succeeded: pullCard.statusKey === "succeeded"
            readonly property bool cancelled: pullCard.statusKey === "cancelled"

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing / 2

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Icon {
                        source: pullCard.failed ? "dialog-error" : pullCard.succeeded ? "dialog-ok-apply" : "download"
                        color: pullCard.failed ? Local.StatusPalette.color("negative")
                            : pullCard.succeeded ? Local.StatusPalette.color("positive")
                            : Kirigami.Theme.textColor
                        implicitWidth: Kirigami.Units.iconSizes.smallMedium
                        implicitHeight: Kirigami.Units.iconSizes.smallMedium
                    }

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: pullCard.reference
                        font.bold: true
                        elide: Text.ElideMiddle
                    }

                    QQC2.BusyIndicator {
                        objectName: "pullBusyIndicator"
                        visible: pullCard.active
                        running: pullCard.active
                        implicitWidth: Kirigami.Units.iconSizes.smallMedium
                        implicitHeight: Kirigami.Units.iconSizes.smallMedium
                    }

                    // 进行中：可以取消这一路（不影响其它镜像的拉取）
                    QQC2.Button {
                        objectName: "cancelPullButton"
                        visible: pullCard.active
                        text: i18n("Cancel")
                        icon.name: "process-stop"
                        flat: true
                        onClicked: root.operations.cancelPull(pullCard.reference)
                    }

                    // 凭据相关的失败（引擎回 401/403）：先去登录，而不是反复重试
                    QQC2.Button {
                        objectName: "pullLoginButton"
                        visible: !pullCard.active && pullCard.statusKey === "failed" && pullCard.errorKindKey === "permissionDenied"
                        text: i18n("Log in…")
                        icon.name: "dialog-password"
                        flat: true
                        onClicked: root.loginRequested(pullCard.reference)
                    }

                    // 已结束：移除这条记录（失败的记录也要用户显式处理掉）
                    QQC2.Button {
                        objectName: "dismissPullButton"
                        visible: !pullCard.active
                        icon.name: "edit-clear"
                        flat: true
                        Accessible.name: i18n("Remove this entry from the list")
                        QQC2.ToolTip.text: i18n("Remove this entry")
                        QQC2.ToolTip.visible: hovered
                        onClicked: root.operations.dismissPull(pullCard.reference)
                    }
                }

                QQC2.ProgressBar {
                    objectName: "pullProgressBar"
                    Layout.fillWidth: true
                    visible: pullCard.active
                    // 总量未知时是不确定态：不能假装知道进度
                    indeterminate: !pullCard.progressKnown
                    from: 0
                    to: 1
                    value: pullCard.progressKnown ? pullCard.progress : 0
                }

                QQC2.Label {
                    objectName: "pullStatusLabel"
                    Layout.fillWidth: true
                    text: {
                        if (pullCard.failed) {
                            return pullCard.detailText.length > 0
                                ? i18n("Pull failed: %1", pullCard.detailText)
                                : i18n("Pull failed.");
                        }
                        if (pullCard.succeeded) {
                            return i18n("Image pulled.");
                        }
                        if (pullCard.cancelled) {
                            return i18n("Image pull cancelled.");
                        }
                        const parts = [];
                        if (pullCard.statusText.length > 0) {
                            parts.push(pullCard.statusText);
                        }
                        if (pullCard.totalLayers > 0) {
                            parts.push(i18n("%1 of %2 layers", pullCard.completedLayers, pullCard.totalLayers));
                        }
                        return parts.join(" · ");
                    }
                    color: pullCard.failed ? Local.StatusPalette.color("negative") : Kirigami.Theme.textColor
                    opacity: pullCard.active ? 0.85 : 1.0
                    wrapMode: Text.WordWrap
                    elide: Text.ElideRight
                    font: Kirigami.Theme.smallFont
                }
            }

            Accessible.name: pullCard.failed ? i18n("Pull failed: %1", pullCard.detailText) : pullCard.reference
        }
    }
}
