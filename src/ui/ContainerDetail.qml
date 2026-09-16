/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Container Detail（ARCH_V2 §7）：把运行信息按用户理解方式重新组织，
    不是 docker inspect JSON 的漂亮化输出。

    信息层级（§7.2）：一级（Name/State/Status/Image/Health）→ 二级（Runtime/Network/Mounts）
    → 三级（Environment/Labels，默认折叠）。
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kcmutils as KCM
import org.kde.kirigami as Kirigami
import org.kde.kontainer as Kontainer

// 详情内容明显高于窗口：必须用可滚动页面（Kirigami.Page 不提供滚动）
KCM.SimpleKCM {
    id: page

    property string containerId: ""

    readonly property var controller: kcm.controller.containerDetail
    readonly property bool ready: controller.loadStateKey === "ready"

    /*! 请求返回列表页（由 main.qml 接 StackView.pop）。
        注意：不能叫 backRequested——Kirigami.Page 已经声明了同名信号。 */
    signal closeRequested

    Component.onCompleted: {
        // 必须无条件 start()：容器 id 相同时（A → 返回 → 再进 A）也要重新 inspect、
        // 重新开始低频复核与 stats 采样（§27/§46）。
        if (page.containerId.length > 0) {
            controller.containerId = page.containerId;
            controller.start();
        }
    }

    // 离开页面：停止 stats 采样并释放指标历史（§27）
    Component.onDestruction: controller.stop()



    function stateColor(stateKey: string, healthKey: string): color {
        switch (Kontainer.Presentation.stateSemanticKey(stateKey, healthKey)) {
        case "positive":
            return Kirigami.Theme.positiveTextColor;
        case "neutral":
            return Kirigami.Theme.neutralTextColor;
        case "negative":
            return Kirigami.Theme.negativeTextColor;
        default:
            return Kirigami.Theme.disabledTextColor;
        }
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            QQC2.Button {
                text: i18n("Containers")
                icon.name: "go-previous"
                onClicked: page.closeRequested()
            }
            Kirigami.Heading {
                Layout.fillWidth: true
                level: 2
                elide: Text.ElideRight
                text: controller.name.length > 0 ? controller.name : i18n("Container")
            }
            QQC2.ToolButton {
                icon.name: "edit-copy"
                display: QQC2.AbstractButton.IconOnly
                enabled: controller.name.length > 0
                QQC2.ToolTip.text: i18n("Copy container name")
                QQC2.ToolTip.visible: hovered
                onClicked: Kontainer.Presentation.copyToClipboard(controller.name)
            }
        }

        // ---------------------------------------------------------------- //
        // Loading / Error（§31：详细失败不影响列表页）                        //
        // ---------------------------------------------------------------- //
        RowLayout {
            Layout.fillWidth: true
            visible: controller.loadStateKey === "loading"
            spacing: Kirigami.Units.smallSpacing

            QQC2.BusyIndicator {
                running: parent.visible
                implicitHeight: Kirigami.Units.iconSizes.smallMedium
                implicitWidth: implicitHeight
            }
            QQC2.Label {
                text: i18n("Loading container details…")
                opacity: 0.7
            }
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: controller.loadStateKey === "error"
            type: Kirigami.MessageType.Error
            text: controller.errorText.length > 0 ? controller.errorText : i18n("Unable to retrieve container details.")
            actions: [
                Kirigami.Action {
                    text: i18n("Retry")
                    icon.name: "view-refresh"
                    onTriggered: controller.refresh()
                },
                Kirigami.Action {
                    text: i18n("Back")
                    icon.name: "go-previous"
                    onTriggered: page.closeRequested()
                }
            ]
        }

        // ---------------------------------------------------------------- //
        // 一级信息                                                          //
        // ---------------------------------------------------------------- //
        Kirigami.FormLayout {
            Layout.fillWidth: true
            visible: page.ready

            RowLayout {
                Kirigami.FormData.label: i18n("State:")

                Kirigami.Icon {
                    source: Kontainer.Presentation.stateIconName(controller.stateKey)
                    color: page.stateColor(controller.stateKey, controller.healthKey)
                    implicitWidth: Kirigami.Units.iconSizes.small
                    implicitHeight: Kirigami.Units.iconSizes.small
                }
                QQC2.Label {
                    text: controller.stateText
                    color: page.stateColor(controller.stateKey, controller.healthKey)
                    font.bold: true
                }
                QQC2.Label {
                    visible: controller.healthKey !== "unknown" && controller.healthKey !== "none"
                    text: controller.healthText
                    font: Kirigami.Theme.smallFont
                    opacity: 0.8
                }
            }

            QQC2.Label {
                Kirigami.FormData.label: i18n("Status:")
                text: controller.status.length > 0 ? controller.status : i18n("Unknown")
            }

            QQC2.Label {
                Kirigami.FormData.label: i18n("Image:")
                text: controller.image
            }

            RowLayout {
                Kirigami.FormData.label: i18n("Container ID:")

                QQC2.Label {
                    text: controller.shortId
                    font.family: "monospace"
                }
                QQC2.ToolButton {
                    icon.name: "edit-copy"
                    text: i18n("Copy")
                    display: QQC2.AbstractButton.IconOnly
                    QQC2.ToolTip.text: i18n("Copy container ID")
                    QQC2.ToolTip.visible: hovered
                    onClicked: Kontainer.Presentation.copyToClipboard(controller.containerId)
                }
            }

            QQC2.Label {
                Kirigami.FormData.label: i18n("Created:")
                visible: Kontainer.Format.isValid(controller.created)
                text: Kontainer.Format.absoluteTime(controller.created)
            }

            QQC2.Label {
                Kirigami.FormData.label: i18n("Started:")
                visible: Kontainer.Format.isValid(controller.started)
                text: i18nc("@info absolute time and relative", "%1 (%2 ago)", Kontainer.Format.absoluteTime(controller.started), Kontainer.Format.elapsed(controller.started))
            }

            QQC2.Label {
                Kirigami.FormData.label: i18n("Finished:")
                visible: Kontainer.Format.isValid(controller.finished)
                text: Kontainer.Format.absoluteTime(controller.finished)
            }
        }

        // ---------------------------------------------------------------- //
        // Runtime                                                          //
        // ---------------------------------------------------------------- //
        Kirigami.Heading {
            level: 3
            visible: page.ready
            text: i18n("Runtime")
        }
        Kirigami.Separator {
            Layout.fillWidth: true
            visible: page.ready
        }

        Kirigami.FormLayout {
            Layout.fillWidth: true
            visible: page.ready

            QQC2.Label {
                Kirigami.FormData.label: i18n("Restart count:")
                text: String(controller.restartCount)
            }
            QQC2.Label {
                Kirigami.FormData.label: i18n("Exit code:")
                text: String(controller.exitCode)
            }
            QQC2.Label {
                Kirigami.FormData.label: i18n("OOM killed:")
                visible: controller.oomKilled
                text: i18n("Yes")
                color: Kirigami.Theme.negativeTextColor
            }
            QQC2.Label {
                Kirigami.FormData.label: i18n("Process ID:")
                visible: controller.pid > 0
                text: String(controller.pid)
            }
            QQC2.Label {
                Kirigami.FormData.label: i18n("Restart policy:")
                visible: text.length > 0
                text: controller.restartPolicy
            }
            QQC2.Label {
                Kirigami.FormData.label: i18n("Platform:")
                visible: text.length > 0
                text: controller.platform
            }
        }

        // ---------------------------------------------------------------- //
        // Resources（§22）                                                  //
        // ---------------------------------------------------------------- //
        ResourceView {
            Layout.fillWidth: true
            visible: page.ready
            metrics: controller.metrics
        }

        // ---------------------------------------------------------------- //
        // Network（§7.2 二级信息）                                          //
        // ---------------------------------------------------------------- //
        Kirigami.Heading {
            level: 3
            visible: page.ready
            text: i18n("Network")
        }
        Kirigami.Separator {
            Layout.fillWidth: true
            visible: page.ready
        }

        QQC2.Label {
            visible: page.ready && controller.networks.empty
            text: i18n("No network information.")
            opacity: 0.7
        }

        Repeater {
            model: controller.networks

            delegate: Kirigami.FormLayout {
                required property string label
                required property string value
                required property string detail
                required property string entryKey

                Layout.fillWidth: true

                QQC2.Label {
                    Kirigami.FormData.label: entryKey === "network-ipv6" ? i18n("IPv6:") : (entryKey === "network-gateway" ? i18n("Gateway:") : i18n("Network:"))
                    text: label
                }
                QQC2.Label {
                    Kirigami.FormData.label: entryKey === "network" ? i18n("IPv4:") : i18n("Address:")
                    visible: value.length > 0
                    text: value
                    font.family: "monospace"
                }
                QQC2.Label {
                    Kirigami.FormData.label: i18n("MAC:")
                    visible: entryKey === "network" && detail.length > 0
                    text: detail
                    font.family: "monospace"
                }
            }
        }

        QQC2.Label {
            Layout.fillWidth: true
            visible: page.ready
            text: i18n("Ports")
            font.bold: true
        }

        QQC2.Label {
            visible: page.ready && controller.ports.empty
            text: i18n("No published ports.")
            opacity: 0.7
        }

        Repeater {
            model: controller.ports

            delegate: RowLayout {
                required property string label
                required property string value

                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                QQC2.Label {
                    text: label
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 6
                }
                QQC2.Label {
                    text: value.length > 0 ? value : i18n("not published")
                    opacity: value.length > 0 ? 1.0 : 0.6
                    Layout.fillWidth: true
                }
            }
        }

        // ---------------------------------------------------------------- //
        // Storage（挂载）                                                    //
        // ---------------------------------------------------------------- //
        Kirigami.Heading {
            level: 3
            visible: page.ready
            text: i18n("Mounts")
        }
        Kirigami.Separator {
            Layout.fillWidth: true
            visible: page.ready
        }

        QQC2.Label {
            visible: page.ready && controller.mounts.empty
            text: i18n("No mounts.")
            opacity: 0.7
        }

        Repeater {
            model: controller.mounts

            delegate: ColumnLayout {
                required property string label
                required property string value
                required property string detail

                Layout.fillWidth: true
                spacing: 0

                QQC2.Label {
                    text: label
                    font.bold: true
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }
                QQC2.Label {
                    // 挂载源路径属于潜在敏感信息（§40）：只在详情页展示，不写日志
                    text: value
                    font: Kirigami.Theme.smallFont
                    opacity: 0.7
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }
                QQC2.Label {
                    text: detail
                    font: Kirigami.Theme.smallFont
                    opacity: 0.6
                }
            }
        }

        // ---------------------------------------------------------------- //
        // Configuration（三级信息：默认折叠，§40）                            //
        // ---------------------------------------------------------------- //
        Kirigami.Heading {
            level: 3
            visible: page.ready
            text: i18n("Configuration")
        }
        Kirigami.Separator {
            Layout.fillWidth: true
            visible: page.ready
        }

        Kirigami.FormLayout {
            Layout.fillWidth: true
            visible: page.ready

            QQC2.Label {
                Kirigami.FormData.label: i18n("Entrypoint:")
                visible: text.length > 0
                text: controller.entrypoint.join(" ")
                font.family: "monospace"
                wrapMode: Text.WrapAnywhere
                Layout.maximumWidth: Kirigami.Units.gridUnit * 28
            }
            QQC2.Label {
                Kirigami.FormData.label: i18n("Command:")
                visible: text.length > 0
                text: controller.command.join(" ")
                font.family: "monospace"
                wrapMode: Text.WrapAnywhere
                Layout.maximumWidth: Kirigami.Units.gridUnit * 28
            }
            QQC2.Label {
                Kirigami.FormData.label: i18n("Working directory:")
                visible: text.length > 0
                text: controller.workingDirectory
                font.family: "monospace"
            }
            QQC2.Label {
                Kirigami.FormData.label: i18n("User:")
                visible: text.length > 0
                text: controller.user
            }
            QQC2.Label {
                Kirigami.FormData.label: i18n("Hostname:")
                visible: text.length > 0
                text: controller.hostname
            }
        }

        // Environment / Labels：默认只显示数量，展开后才渲染真实值（§40）
        ColumnLayout {
            Layout.fillWidth: true
            visible: page.ready
            spacing: 0

            QQC2.ItemDelegate {
                Layout.fillWidth: true
                text: i18ncp("@info environment variable count", "Environment (%1 variable)", "Environment (%1 variables)", controller.environmentCount)
                onClicked: environmentValues.expanded = !environmentValues.expanded

                contentItem: RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Icon {
                        source: environmentValues.expanded ? "arrow-down" : "arrow-right"
                        implicitWidth: Kirigami.Units.iconSizes.small
                        implicitHeight: Kirigami.Units.iconSizes.small
                    }
                    QQC2.Label {
                        text: parent.parent.text
                        Layout.fillWidth: true
                    }
                    QQC2.Label {
                        visible: !environmentValues.expanded
                        text: i18n("hidden by default")
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                        opacity: 0.6
                    }
                }
            }

            Kirigami.Separator {
                Layout.fillWidth: true
            }

            // §40：默认只显示数量；展开后才渲染取值。用 visible 控制（ColumnLayout 会忽略不可见子项）
            ColumnLayout {
                id: environmentValues

                objectName: "environmentValues"
                property bool expanded: false
                visible: expanded
                Layout.fillWidth: true
                spacing: 0

                Repeater {
                    model: controller.environmentVariables

                    delegate: RowLayout {
                        required property string label
                        required property string value

                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        QQC2.Label {
                            text: label
                            font.family: "monospace"
                            Layout.preferredWidth: Kirigami.Units.gridUnit * 10
                            elide: Text.ElideRight
                        }
                        QQC2.Label {
                            text: value
                            font.family: "monospace"
                            Layout.fillWidth: true
                            elide: Text.ElideMiddle
                        }
                    }
                }
            }
        }

        // Labels 同样默认折叠（§40），展开后才渲染取值
        ColumnLayout {
            Layout.fillWidth: true
            visible: page.ready
            spacing: 0

            QQC2.ItemDelegate {
                Layout.fillWidth: true
                text: i18ncp("@info label count", "Labels (%1)", "Labels (%1)", controller.labels.count)
                onClicked: labelValues.expanded = !labelValues.expanded

                contentItem: RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Icon {
                        source: labelValues.expanded ? "arrow-down" : "arrow-right"
                        implicitWidth: Kirigami.Units.iconSizes.small
                        implicitHeight: Kirigami.Units.iconSizes.small
                    }
                    QQC2.Label {
                        text: parent.parent.text
                        Layout.fillWidth: true
                    }
                    QQC2.Label {
                        visible: !labelValues.expanded
                        text: i18n("hidden by default")
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                        opacity: 0.6
                    }
                }
            }

            Kirigami.Separator {
                Layout.fillWidth: true
            }

            ColumnLayout {
                id: labelValues

                objectName: "labelValues"
                property bool expanded: false
                visible: expanded
                Layout.fillWidth: true
                spacing: 0

                Repeater {
                    model: controller.labels

                    delegate: RowLayout {
                        required property string label
                        required property string value

                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        QQC2.Label {
                            text: label
                            font.family: "monospace"
                            Layout.preferredWidth: Kirigami.Units.gridUnit * 10
                            elide: Text.ElideRight
                        }
                        QQC2.Label {
                            text: value
                            font.family: "monospace"
                            Layout.fillWidth: true
                            elide: Text.ElideMiddle
                        }
                    }
                }
            }
        }
    }
}
