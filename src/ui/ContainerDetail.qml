/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Container Detail（ARCH_V2 §7 / ARCH_V3 §2.2）：

    把运行信息按用户理解方式重新组织，不是 docker inspect JSON 的漂亮化输出。

    分区（§2.2）：概览 / 资源 / 网络 / 挂载 / 日志（占位）。
    - 概览：一级信息（Name/State/Health/Status/Image/ID/时间）+ Runtime + Configuration
            + Environment/Labels（默认折叠，§40）
    - 资源：CPU / 内存 / 网络 / 块 IO + 短期趋势
    - 网络：网络接口 + 端口
    - 挂载：Bind / Volume
    - 日志：四期实现，这里明确说明而不是留空白页

    页面本身不滚动：每个分区各自滚动（§2.2 约束 4），
    因此基类用 KCM.AbstractKCM 而不是 SimpleKCM。

    ARCH_V3 §2.1：本文件不做状态语义判断，语义 key 与图标名都来自 C++。
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kcmutils as KCM
import org.kde.kirigami as Kirigami
import org.kde.kontainer as Kontainer

import "components" as Components

KCM.AbstractKCM {
    id: page

    property string containerId: ""

    readonly property var controller: kcm.controller.containerDetail
    readonly property bool ready: controller.loadStateKey === "ready"
    /*! 健康问题优先于状态（Unhealthy 的 Running 必须看起来有问题，§11.3） */
    readonly property string stateSemanticKey: Kontainer.Presentation.stateSemanticKey(controller.stateKey, controller.healthKey)
    readonly property bool healthVisible: controller.healthKey !== "unknown" && controller.healthKey !== "none"
    /*! 正文最大宽度：约 42 gridUnit，宽窗口下避免一行过长（§1.2）。 */
    readonly property real contentMaxWidth: Kirigami.Units.gridUnit * 42

    /*! 网络条目里地址行的字段名：IPv4 / IPv6 / 网关。 */
    function networkValueLabel(entryKey: string): string {
        if (entryKey === "network-ipv6") {
            return i18n("IPv6:");
        }
        if (entryKey === "network-gateway") {
            return i18n("Gateway:");
        }
        return i18n("IPv4:");
    }

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

    /*!
        四期扩展点：破坏性/状态操作按钮统一放在 KCM.AbstractKCM 的 footer 里
        （ARCH_V3 §2.2 约束 5）。三期仍然只读，因此 footer 保持为空——
        不放没有功能的按钮占位。
    */

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        /* ------------------------------------------------------------------ */
        /* 页头：返回 + 名称 + 复制名称                                          */
        /* ------------------------------------------------------------------ */
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.smallSpacing
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
                text: page.controller.name.length > 0 ? page.controller.name : i18n("Container")
            }
            Components.CopyButton {
                value: page.controller.name
                fieldLabel: i18n("container name")
            }
        }

        /* ------------------------------------------------------------------ */
        /* Loading / Error（§31：详细失败不影响列表页）                          */
        /* ------------------------------------------------------------------ */
        RowLayout {
            Layout.fillWidth: true
            visible: page.controller.loadStateKey === "loading"
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
            visible: page.controller.loadStateKey === "error"
            type: Kirigami.MessageType.Error
            text: page.controller.errorText.length > 0 ? page.controller.errorText : i18n("Unable to retrieve container details.")
            actions: [
                Kirigami.Action {
                    text: i18n("Retry")
                    icon.name: "view-refresh"
                    onTriggered: page.controller.refresh()
                },
                Kirigami.Action {
                    text: i18n("Back")
                    icon.name: "go-previous"
                    onTriggered: page.closeRequested()
                }
            ]
        }

        /* ------------------------------------------------------------------ */
        /* 分区切换（§1.3：为日志与后续操作留出位置）                            */
        /* ------------------------------------------------------------------ */
        QQC2.TabBar {
            id: sectionBar

            objectName: "detailTabBar"
            Layout.fillWidth: true
            visible: page.ready

            QQC2.TabButton {
                text: i18nc("@title:tab container overview", "Overview")
            }
            QQC2.TabButton {
                text: i18nc("@title:tab container resources", "Resources")
            }
            QQC2.TabButton {
                text: i18nc("@title:tab container network", "Network")
            }
            QQC2.TabButton {
                text: i18nc("@title:tab container mounts", "Mounts")
            }
            QQC2.TabButton {
                text: i18nc("@title:tab container logs", "Logs")
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
            visible: page.ready
        }

        StackLayout {
            id: sectionStack

            objectName: "detailSectionStack"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: page.ready
            // 切换分区不触碰 controller 生命周期：不重新 inspect、不重启动 stats 采样（§2.2 约束 2/3）
            currentIndex: sectionBar.currentIndex

            /* ============================ 概览 ============================ */
            QQC2.ScrollView {
                id: overviewScroll

                clip: true
                contentWidth: availableWidth

                ColumnLayout {
                    width: Math.min(overviewScroll.availableWidth, page.contentMaxWidth)
                    x: Math.max(0, (overviewScroll.availableWidth - width) / 2)
                    spacing: Kirigami.Units.largeSpacing

                    /* ---------------- 一级信息 ---------------- */
                    Kirigami.FormLayout {
                        Layout.fillWidth: true

                        RowLayout {
                            Kirigami.FormData.label: i18n("State:")

                            Components.StatusChip {
                                semanticKey: page.stateSemanticKey
                                iconName: Kontainer.Presentation.stateIconName(page.controller.stateKey)
                                text: page.controller.stateText
                            }
                            QQC2.Label {
                                visible: page.healthVisible
                                text: page.controller.healthText
                                font: Kirigami.Theme.smallFont
                                opacity: 0.8
                            }
                        }

                        QQC2.Label {
                            Kirigami.FormData.label: i18n("Status:")
                            text: page.controller.status.length > 0 ? page.controller.status : i18n("Unknown")
                        }

                        QQC2.Label {
                            Kirigami.FormData.label: i18n("Image:")
                            Layout.fillWidth: true
                            text: page.controller.image
                            elide: Text.ElideMiddle
                        }

                        Components.CopyableText {
                            Kirigami.FormData.label: i18n("Container ID:")
                            value: page.controller.shortId
                            copyValue: page.controller.containerId
                            fieldLabel: i18n("container ID")
                        }

                        QQC2.Label {
                            Kirigami.FormData.label: i18n("Created:")
                            visible: Kontainer.Format.isValid(page.controller.created)
                            text: Kontainer.Format.absoluteTime(page.controller.created)
                        }

                        QQC2.Label {
                            Kirigami.FormData.label: i18n("Started:")
                            visible: Kontainer.Format.isValid(page.controller.started)
                            text: i18nc("@info absolute time and relative", "%1 (%2 ago)", Kontainer.Format.absoluteTime(page.controller.started), Kontainer.Format.elapsed(page.controller.started))
                        }

                        QQC2.Label {
                            Kirigami.FormData.label: i18n("Finished:")
                            visible: Kontainer.Format.isValid(page.controller.finished)
                            text: Kontainer.Format.absoluteTime(page.controller.finished)
                        }
                    }

                    /* ---------------- Runtime ---------------- */
                    Kirigami.Heading {
                        level: 3
                        text: i18n("Runtime")
                    }
                    Kirigami.Separator {
                        Layout.fillWidth: true
                    }

                    Kirigami.FormLayout {
                        Layout.fillWidth: true

                        QQC2.Label {
                            Kirigami.FormData.label: i18n("Restart count:")
                            text: String(page.controller.restartCount)
                        }
                        QQC2.Label {
                            Kirigami.FormData.label: i18n("Exit code:")
                            text: String(page.controller.exitCode)
                        }
                        QQC2.Label {
                            Kirigami.FormData.label: i18n("OOM killed:")
                            visible: page.controller.oomKilled
                            text: i18n("Yes")
                            color: Components.StatusPalette.color("negative")
                        }
                        QQC2.Label {
                            Kirigami.FormData.label: i18n("Process ID:")
                            visible: page.controller.pid > 0
                            text: String(page.controller.pid)
                        }
                        QQC2.Label {
                            Kirigami.FormData.label: i18n("Restart policy:")
                            visible: text.length > 0
                            text: page.controller.restartPolicy
                        }
                        QQC2.Label {
                            Kirigami.FormData.label: i18n("Platform:")
                            visible: text.length > 0
                            text: page.controller.platform
                        }
                    }

                    /* ---------------- Configuration ---------------- */
                    Kirigami.Heading {
                        level: 3
                        text: i18n("Configuration")
                    }
                    Kirigami.Separator {
                        Layout.fillWidth: true
                    }

                    Kirigami.FormLayout {
                        Layout.fillWidth: true

                        QQC2.Label {
                            Kirigami.FormData.label: i18n("Entrypoint:")
                            visible: text.length > 0
                            text: page.controller.entrypoint.join(" ")
                            font.family: "monospace"
                            wrapMode: Text.WrapAnywhere
                            Layout.maximumWidth: Kirigami.Units.gridUnit * 28
                        }
                        QQC2.Label {
                            Kirigami.FormData.label: i18n("Command:")
                            visible: text.length > 0
                            text: page.controller.command.join(" ")
                            font.family: "monospace"
                            wrapMode: Text.WrapAnywhere
                            Layout.maximumWidth: Kirigami.Units.gridUnit * 28
                        }
                        QQC2.Label {
                            Kirigami.FormData.label: i18n("Working directory:")
                            visible: text.length > 0
                            text: page.controller.workingDirectory
                            font.family: "monospace"
                        }
                        QQC2.Label {
                            Kirigami.FormData.label: i18n("User:")
                            visible: text.length > 0
                            text: page.controller.user
                        }
                        QQC2.Label {
                            Kirigami.FormData.label: i18n("Hostname:")
                            visible: text.length > 0
                            text: page.controller.hostname
                        }
                    }

                    /* ---------------- Environment / Labels（默认折叠） ---------------- */
                    Components.CollapsibleSection {
                        Layout.fillWidth: true
                        contentObjectName: "environmentValues"
                        title: i18ncp("@info environment variable count", "Environment (%1 variable)", "Environment (%1 variables)", page.controller.environmentCount)

                        Components.KeyValueList {
                            model: page.controller.environmentVariables
                        }
                    }

                    Components.CollapsibleSection {
                        Layout.fillWidth: true
                        contentObjectName: "labelValues"
                        title: i18ncp("@info label count", "Labels (%1)", "Labels (%1)", page.controller.labels.count)

                        Components.KeyValueList {
                            model: page.controller.labels
                        }
                    }
                }
            }

            /* ============================ 资源 ============================ */
            QQC2.ScrollView {
                id: resourcesScroll

                clip: true
                contentWidth: availableWidth

                ResourceView {
                    width: Math.min(resourcesScroll.availableWidth, page.contentMaxWidth)
                    x: Math.max(0, (resourcesScroll.availableWidth - width) / 2)
                    metrics: page.controller.metrics
                }
            }

            /* ============================ 网络 ============================ */
            QQC2.ScrollView {
                id: networkScroll

                clip: true
                contentWidth: availableWidth

                ColumnLayout {
                    width: Math.min(networkScroll.availableWidth, page.contentMaxWidth)
                    x: Math.max(0, (networkScroll.availableWidth - width) / 2)
                    spacing: Kirigami.Units.smallSpacing

                    Components.EmptyPlaceholder {
                        Layout.fillWidth: true
                        message: page.controller.networks.empty ? i18n("No network information.") : ""
                    }

                    /*  网络条目**不用 FormLayout**：每个条目一个 GridLayout 落在
                        Repeater 里，正是 core dump 中「外层布局 → 条目 box → 内层
                        GridLayout sizeHint → 查 FormData 附加属性」的形状。
                        改用与「挂载」一致的普通行布局，去掉这层嵌套。 */
                    Repeater {
                        model: page.controller.networks

                        delegate: ColumnLayout {
                            required property string label
                            required property string value
                            required property string detail
                            required property string entryKey

                            objectName: "networkEntry"
                            Layout.fillWidth: true
                            spacing: 0

                            /* 网络名只在主条目显示：IPv6 / 网关条目里 label 就是
                               "IPv6"/"Gateway" 这类标题，重复展示会变成 "IPv6: IPv6" */
                            RowLayout {
                                Layout.fillWidth: true
                                visible: entryKey === "network"
                                spacing: Kirigami.Units.smallSpacing

                                QQC2.Label {
                                    text: i18n("Network:")
                                    Layout.preferredWidth: Kirigami.Units.gridUnit * 6
                                    opacity: 0.75
                                    elide: Text.ElideRight
                                }
                                QQC2.Label {
                                    text: label
                                    font.family: "monospace"
                                    Layout.fillWidth: true
                                    elide: Text.ElideMiddle
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                visible: value.length > 0
                                spacing: Kirigami.Units.smallSpacing

                                QQC2.Label {
                                    text: page.networkValueLabel(entryKey)
                                    Layout.preferredWidth: Kirigami.Units.gridUnit * 6
                                    opacity: 0.75
                                    elide: Text.ElideRight
                                }
                                QQC2.Label {
                                    text: value
                                    font.family: "monospace"
                                    Layout.fillWidth: true
                                    elide: Text.ElideMiddle
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                visible: entryKey === "network" && detail.length > 0
                                spacing: Kirigami.Units.smallSpacing

                                QQC2.Label {
                                    text: i18n("MAC:")
                                    Layout.preferredWidth: Kirigami.Units.gridUnit * 6
                                    opacity: 0.75
                                }
                                QQC2.Label {
                                    text: detail
                                    font.family: "monospace"
                                    Layout.fillWidth: true
                                    elide: Text.ElideMiddle
                                }
                            }
                        }
                    }

                    Kirigami.Heading {
                        level: 3
                        text: i18n("Ports")
                    }
                    Kirigami.Separator {
                        Layout.fillWidth: true
                    }

                    Components.EmptyPlaceholder {
                        Layout.fillWidth: true
                        message: page.controller.ports.empty ? i18n("No published ports.") : ""
                    }

                    Repeater {
                        model: page.controller.ports

                        delegate: RowLayout {
                            required property string label
                            required property string value

                            objectName: "portEntry"
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            // 端口映射是「一行一条」：标签列固定宽度并省略，
                            // 避免长标签把值挤到第二行（§1.5 排版）
                            QQC2.Label {
                                text: label
                                elide: Text.ElideRight
                                Layout.preferredWidth: Kirigami.Units.gridUnit * 8
                            }
                            QQC2.Label {
                                text: value.length > 0 ? value : i18n("not published")
                                opacity: value.length > 0 ? 1.0 : 0.6
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                    }
                }
            }

            /* ============================ 挂载 ============================ */
            QQC2.ScrollView {
                id: mountsScroll

                clip: true
                contentWidth: availableWidth

                ColumnLayout {
                    width: Math.min(mountsScroll.availableWidth, page.contentMaxWidth)
                    x: Math.max(0, (mountsScroll.availableWidth - width) / 2)
                    spacing: Kirigami.Units.smallSpacing

                    Components.EmptyPlaceholder {
                        Layout.fillWidth: true
                        message: page.controller.mounts.empty ? i18n("No mounts.") : ""
                    }

                    Repeater {
                        model: page.controller.mounts

                        delegate: ColumnLayout {
                            required property string label
                            required property string value
                            required property string detail

                            objectName: "mountEntry"
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
                }
            }

            /* ============================ 日志（占位） ============================ */
            ColumnLayout {
                Layout.margins: Kirigami.Units.largeSpacing

                Item {
                    Layout.fillHeight: true
                }

                Components.EmptyPlaceholder {
                    objectName: "logsPlaceholder"
                    Layout.fillWidth: true
                    iconName: "view-list-text"
                    message: i18n("Container logs are not available yet.")
                    explanationText: i18n("Streaming logs will be added in a later version. Until then, use the docker CLI or your container's own log destination.")
                }

                Item {
                    Layout.fillHeight: true
                }
            }
        }
    }
}
