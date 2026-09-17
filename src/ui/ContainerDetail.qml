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
    /*! 写操作控制器（ARCH_V4 §2.3）。 */
    readonly property var operations: kcm.controller.operations
    readonly property bool ready: controller.loadStateKey === "ready"
    /*! 健康问题优先于状态（Unhealthy 的 Running 必须看起来有问题，§11.3） */
    readonly property string stateSemanticKey: Kontainer.Presentation.stateSemanticKey(controller.stateKey, controller.healthKey)
    readonly property bool healthVisible: controller.healthKey !== "unknown" && controller.healthKey !== "none"
    /*! 正文最大宽度：约 42 gridUnit，宽窗口下避免一行过长（§1.2）。 */
    readonly property real contentMaxWidth: Kirigami.Units.gridUnit * 42

    /*!
        写操作可见性（ARCH_V4 §2.3）：
        - 权限门不允许写时，整条 footer 不出现（不是禁用后静默）
        - 可逆操作（启动 / 停止 / 重启）直接执行；删除必须二次确认
        - 运行中的容器不给删除按钮，并说明原因：让引擎返回 409 再解释是下策
    */
    /*! 宿主节点标题：daemon 报告的 Name 就是宿主机名（没有时退化为本机回环名）。 */
    readonly property string engineHostName: kcm.controller.engine.engineName.length > 0 ? kcm.controller.engine.engineName : i18n("Host")

    readonly property bool targetBusy: {
        // 同上：函数调用本身不建立依赖，必须先读 stateRevision
        page.operations.stateRevision;
        return page.operations.isContainerBusy(page.containerId);
    }
    readonly property bool canStart: page.ready && !page.targetBusy && page.operations.writeAllowed
        && (controller.stateKey === "exited" || controller.stateKey === "created" || controller.stateKey === "dead")
    readonly property bool canStop: page.ready && !page.targetBusy && page.operations.writeAllowed
        && (controller.stateKey === "running" || controller.stateKey === "paused" || controller.stateKey === "restarting")
    readonly property bool canRestart: page.ready && !page.targetBusy && page.operations.writeAllowed
        && (controller.stateKey === "running" || controller.stateKey === "paused")
    readonly property bool canRemove: page.ready && !page.targetBusy && page.operations.writeAllowed
        && controller.stateKey !== "running" && controller.stateKey !== "paused" && controller.stateKey !== "restarting"

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
        写操作 footer（ARCH_V3 §2.2 约束 5 预留的位置，ARCH_V4 §2.3 填充）。

        可逆操作与破坏性操作在同一行，但删除按钮在最右侧并单独确认；
        忙碌时按钮禁用并显示进度指示，避免重复点击产生第二个请求。
    */
    footer: QQC2.ToolBar {
        id: actionBar

        objectName: "containerActionBar"
        visible: page.operations.writeAllowed
        position: QQC2.ToolBar.Footer

        contentItem: RowLayout {
            spacing: Kirigami.Units.smallSpacing

            QQC2.Button {
                objectName: "detailStartButton"
                visible: page.canStart
                text: i18n("Start")
                icon.name: "media-playback-start"
                onClicked: page.operations.startContainer(page.containerId)
            }

            QQC2.Button {
                objectName: "detailStopButton"
                visible: page.canStop
                text: i18n("Stop")
                icon.name: "media-playback-stop"
                onClicked: page.operations.stopContainer(page.containerId)
            }

            QQC2.Button {
                objectName: "detailRestartButton"
                visible: page.canRestart
                text: i18n("Restart")
                icon.name: "view-refresh"
                onClicked: page.operations.restartContainer(page.containerId)
            }

            QQC2.BusyIndicator {
                objectName: "detailBusyIndicator"
                visible: page.targetBusy
                running: page.targetBusy
                implicitWidth: Kirigami.Units.iconSizes.smallMedium
                implicitHeight: Kirigami.Units.iconSizes.smallMedium
            }

            // 运行中不给删除：说明原因比让引擎报 409 更直接
            QQC2.Label {
                objectName: "removeBlockedHint"
                visible: page.ready && !page.canRemove && !page.targetBusy && page.operations.writeAllowed
                    && (controller.stateKey === "running" || controller.stateKey === "paused" || controller.stateKey === "restarting")
                text: i18n("Stop the container to delete it.")
                font: Kirigami.Theme.smallFont
                opacity: 0.7
            }

            Item {
                Layout.fillWidth: true
            }

            QQC2.Button {
                objectName: "detailRemoveButton"
                visible: page.canRemove
                text: i18n("Delete")
                icon.name: "edit-delete"
                onClicked: removeDialog.open()
            }
        }
    }


    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        /* 本页触发的操作结果（启动 / 停止 / 重启 / 删除）在这里呈现 */
        Components.OperationMessage {
            Layout.fillWidth: true
            operations: page.operations
        }

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
                        text: i18n("Port mapping")
                    }
                    Kirigami.Separator {
                        Layout.fillWidth: true
                    }

                    Components.EmptyPlaceholder {
                        Layout.fillWidth: true
                        objectName: "portsEmptyPlaceholder"
                        message: page.controller.publishedPorts.empty && page.controller.unpublishedPorts.empty ? i18n("No published ports.") : ""
                    }

                    /* 拓扑：左列容器端口、右列宿主绑定，连线为装饰（§2.1.2） */
                    Components.PortTopology {
                        Layout.fillWidth: true
                        visible: !page.controller.publishedPorts.empty
                        model: page.controller.publishedPorts
                        containerLabel: page.controller.name.length > 0 ? page.controller.name : i18n("Container")
                        hostLabel: page.engineHostName
                        // 同一个容器永远同色：种子就是容器 id（ARCH_V4 §2.1.2）
                        colorSeed: page.controller.containerId
                    }

                    /* 只 EXPOSE、没有映射到宿主的端口：没有宿主端点，因此不画线 */
                    QQC2.Label {
                        Layout.fillWidth: true
                        Layout.topMargin: Kirigami.Units.smallSpacing
                        visible: !page.controller.unpublishedPorts.empty
                        text: i18n("Exposed but not published")
                        font.bold: true
                    }

                    Flow {
                        Layout.fillWidth: true
                        visible: !page.controller.unpublishedPorts.empty
                        spacing: Kirigami.Units.smallSpacing

                        Repeater {
                            model: page.controller.unpublishedPorts

                            delegate: Components.FieldChip {
                                required property string containerChipText
                                required property string protocol

                                objectName: "unpublishedPortChip"
                                muted: true
                                text: containerChipText
                                Accessible.name: i18n("%1, exposed but not published to the host", containerChipText)
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
                        objectName: "mountsEmptyPlaceholder"
                        message: page.controller.mounts.empty ? i18n("No mounts.") : ""
                    }

                    /* 打开宿主目录失败时的提示（路径不存在 / 没有文件管理器） */
                    Kirigami.InlineMessage {
                        objectName: "mountActionMessage"
                        Layout.fillWidth: true
                        visible: page.controller.mountActionError.length > 0
                        type: Kirigami.MessageType.Warning
                        text: page.controller.mountActionError
                        showCloseButton: true
                        onVisibleChanged: {
                            if (!visible) {
                                page.controller.dismissMountActionError();
                            }
                        }
                    }

                    Repeater {
                        model: page.controller.mounts

                        delegate: ColumnLayout {
                            id: mountRow

                            required property int index
                            required property string typeKey
                            required property string source
                            required property string destination
                            required property string mode
                            required property string volumeName
                            required property string sourceStateKey
                            required property bool openable

                            objectName: "mountEntry"
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing / 2

                            /* 第一行：类型 + 读写模式 + 宿主路径缺失警告 */
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Kirigami.Units.smallSpacing

                                Components.FieldChip {
                                    objectName: "mountTypeChip"
                                    text: mountRow.typeKey.length > 0 ? mountRow.typeKey : i18n("mount")
                                }

                                Components.FieldChip {
                                    objectName: "mountModeChip"
                                    text: mountRow.mode
                                    muted: mountRow.mode === "ro"
                                }

                                // 命名卷显示卷名，否则用户只看到一个宿主路径
                                Components.FieldChip {
                                    objectName: "mountVolumeChip"
                                    visible: mountRow.volumeName.length > 0
                                    text: mountRow.volumeName
                                }

                                Item {
                                    Layout.fillWidth: true
                                }

                                Components.StatusChip {
                                    objectName: "mountSourceWarning"
                                    visible: mountRow.sourceStateKey === "missing" || mountRow.sourceStateKey === "notADirectory"
                                    semanticKey: "neutral"
                                    iconName: "dialog-warning"
                                    text: mountRow.sourceStateKey === "notADirectory" ? i18n("Not a directory") : i18n("Host path missing")
                                }
                            }

                            /* 第二行：宿主路径 → 容器路径
                               （容器路径**靠右**收尾，两条路径都从中间省略：太长也不挤掉对方） */
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Kirigami.Units.smallSpacing
                                visible: mountRow.source.length > 0

                                QQC2.Label {
                                    objectName: "mountSourceLabel"
                                    // 挂载源路径属潜在敏感信息（§40）：只在详情页展示，不写日志
                                    text: mountRow.source
                                    font.family: "monospace"
                                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                                    // 宿主路径通常更长：占满剩余宽度，从中间省略
                                    // （保留开头与尾段，比单纯截尾更有辨识度）
                                    elide: Text.ElideMiddle
                                    horizontalAlignment: Text.AlignLeft
                                    Layout.fillWidth: true
                                }

                                Kirigami.Icon {
                                    source: "go-next-symbolic"
                                    Accessible.ignored: true
                                    implicitWidth: Kirigami.Units.iconSizes.small
                                    implicitHeight: Kirigami.Units.iconSizes.small
                                    opacity: 0.6
                                }

                                QQC2.Label {
                                    objectName: "mountDestinationLabel"
                                    text: mountRow.destination
                                    font.family: "monospace"
                                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                                    elide: Text.ElideMiddle
                                    // 靠右对齐：容器路径是固定长度、需要一眼扫完的一列，
                                    // 让它贴着右边缘（而不是跟着宿主路径的长度左右漂移）
                                    horizontalAlignment: Text.AlignRight
                                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                                    // 很长的容器路径最多占四成宽度，剩下的留给宿主路径
                                    Layout.maximumWidth: Math.max(Kirigami.Units.gridUnit * 6, mountRow.width * 0.4)
                                }
                            }

                            /* 第三行：动作（tmpfs 与缺失路径不提供打开动作） */
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Kirigami.Units.smallSpacing

                                QQC2.Button {
                                    objectName: "mountOpenButton"
                                    visible: mountRow.openable
                                    text: i18n("Open host folder")
                                    icon.name: "folder-open"
                                    onClicked: page.controller.openMountHostPath(mountRow.index)
                                }

                                Components.CopyButton {
                                    visible: mountRow.source.length > 0
                                    value: mountRow.source
                                    fieldLabel: i18n("host path")
                                }

                                Components.CopyButton {
                                    value: mountRow.destination
                                    fieldLabel: i18n("container path")
                                }
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

    /* 删除确认：句式与后果说明固定（ARCH_V4 §2.2.5） */
    Components.ConfirmDialog {
        id: removeDialog

        objectName: "removeContainerDialog"
        headingText: i18n("Delete container")
        questionText: i18n("Delete the container “%1”?", controller.name)
        consequenceText: i18n("The container is removed. Its anonymous and named volumes are kept.")
        acceptText: i18n("Delete")
        destructive: true
        onConfirmed: page.operations.removeContainer(page.containerId)
    }

    /* 删除成功后本页的目标已经不存在：返回列表（列表已由控制器刷新） */
    Connections {
        target: page.operations
        function onContainerRemoved(id) {
            if (id === page.containerId) {
                page.closeRequested();
            }
        }
    }
}
