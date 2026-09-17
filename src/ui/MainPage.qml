/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Kontainer 首页（ARCH_V2 §5 / ARCH_V3 §2）：Engine 状态 + Overview + Storage + 容器/镜像列表。

    由 main.qml 放进 StackView 作为根页面；卡片激活时发出信号，由 main.qml 负责导航。

    ARCH_V3 §2.1：本文件不做任何状态语义判断——
    状态语义（positive/neutral/negative）与图标名全部来自 C++（StatusController / Presentation），
    这里只负责排版与文案。
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.kontainer as Kontainer

import "components" as Components

Kirigami.Page {
    id: root

    readonly property var controller: kcm.controller
    readonly property var containerList: controller.containerList
    readonly property var imageList: controller.imageList
    readonly property var networkList: controller.networkList
    readonly property var volumeList: controller.volumeList

    /*! 卡片被激活：由 main.qml 接到导航上（ARCH_V2 §43：导航属于 KCM 层） */
    signal containerActivated(string containerId)
    /*! 请求打开「运行时配置」页（daemon.json）；scope = user | system。 */
    signal configureRuntimeRequested(string scope)
    /*! 打开仓库认证页（ARCH_V5_V8 §2.7）：镜像标签页工具栏与失败引导都用它。 */
    signal registryAuthRequested(string serverAddress)
    signal imageActivated(string imageId)
    /*! 打开网络详情（六期 §3.2）；由 main.qml 负责导航。 */
    signal networkActivated(string networkId)
    /*! 构建镜像的内联表单是否展开（八期 §5.4）。 */
    property bool buildPanelOpen: false

    /*! 打开数据卷详情（六期 §3.5）。 */
    signal volumeActivated(string volumeName)
    /*! 打开创建容器向导（七期 §4.4）；presetImage 为空表示从零开始。 */
    signal createContainerRequested(string presetImage)

    /*! 数据卷页的内联面板（创建 / 清理）。 */
    property bool volumeCreatePanelOpen: false
    property bool volumePrunePanelOpen: false

    /*! 可回收空间文案：只统计**已知**大小，并且明说还有几个卷的大小未知。 */
    function pruneReclaimableText(): string {
        const model = root.controller.volumeModel;
        const known = model.knownUnusedSize();
        const unknown = model.unknownUnusedSizeCount();
        let text = Kontainer.Format.byteSize(known);
        if (unknown > 0) {
            text += " + " + i18ncp("@info volumes with unknown size",
                                   "one volume of unknown size",
                                   "%1 volumes of unknown size",
                                   unknown);
        }
        return text;
    }

    /*! Overview 统计块（纯展示层聚合；semanticKey 为空表示该项没有状态语义） */
    readonly property var tiles: [
        {
            label: i18n("Containers"),
            value: controller.engine.containerTotal,
            icon: "application-x-executable",
            semanticKey: ""
        },
        {
            label: i18n("Running"),
            value: controller.engine.containersRunning,
            icon: "media-playback-start",
            semanticKey: "positive"
        },
        {
            label: i18n("Paused"),
            value: controller.engine.containersPaused,
            icon: "media-playback-pause",
            semanticKey: "neutral"
        },
        {
            label: i18n("Stopped"),
            value: controller.engine.containersStopped,
            icon: "media-playback-stop",
            semanticKey: "negative"
        },
        {
            label: i18n("Images"),
            value: controller.engine.imageCount,
            icon: "image-x-generic",
            semanticKey: ""
        }
    ]

    readonly property bool countsReady: controller.engine.countsAvailable

    /*! 写操作控制器（ARCH_V4 §2.2.4）：卡片按钮与结果提示都从这里读。 */
    readonly property var operations: root.controller.operations

    function engineStateText(stateKey: string): string {
        switch (stateKey) {
        case "loading":
            return i18n("Loading Docker status…");
        case "ready":
        case "refreshing":
        case "partial":
            return i18n("Connected");
        default:
            return i18n("Not connected");
        }
    }

    /* ------------------------------------------------------------------ */
    /* 空状态（§33：四种情况必须区分；判定逻辑在页面，呈现交给组件）           */
    /* ------------------------------------------------------------------ */

    readonly property var containersEmptyState: {
        if (root.containerList.count > 0 || root.controller.containersStateKey === "error") {
            return {
                message: "",
                actionText: ""
            };
        }
        if (root.controller.containers.count === 0) {
            return {
                message: i18n("No containers found."),
                actionText: ""
            };
        }
        if (root.containerList.searchText.length > 0) {
            return {
                message: i18nc("@info no container matches the search", "No containers match “%1”.", root.containerList.searchText),
                actionText: i18n("Clear search")
            };
        }
        return {
            message: i18n("No containers match the current filter."),
            actionText: i18n("Show all containers")
        };
    }

    readonly property var imagesEmptyState: {
        if (root.imageList.count > 0 || root.controller.imagesStateKey === "error") {
            return {
                message: "",
                actionText: ""
            };
        }
        if (root.controller.images.count === 0) {
            return {
                message: i18n("No images found."),
                // 权限允许时给出真正能解决问题的动作（ARCH_V3_pre §1.3：空状态可以带一个操作按钮）
                actionText: root.operations.writeAllowed ? i18n("Pull an image") : ""
            };
        }
        if (root.imageList.searchText.length > 0) {
            return {
                message: i18nc("@info no image matches the search", "No images match “%1”.", root.imageList.searchText),
                actionText: i18n("Clear search")
            };
        }
        return {
            message: i18n("No images match the current filter."),
            actionText: i18n("Show all images")
        };
    }

    /*!
        清空当前标签页的搜索与过滤条件。

        注意：用户一旦在输入框里打过字，TextField.text 的声明式绑定就会被内部赋值打断
        （这是 QQC2 的行为，不是本页的 bug），因此这里必须同时显式清空输入框与下拉框。
    */
    /*!
        空状态里的引导动作：镜像列表为空时是「拉取一个镜像」，其余情况是清空条件。
    */
    function handleEmptyAction() {
        if (tabBar.currentIndex === 1 && root.controller.images.count === 0 && root.operations.writeAllowed) {
            pullDialog.reset();
            pullDialog.open();
            return;
        }
        root.clearCurrentTabFilters();
    }

    function clearCurrentTabFilters() {
        if (tabBar.currentIndex === 0) {
            root.containerList.searchText = "";
            root.containerList.stateFilter = "all";
        } else {
            root.imageList.searchText = "";
            root.imageList.useFilter = "all";
        }
        searchField.text = "";
        filterBox.currentIndex = 0;
    }

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        /* ------------------------------------------------------------------ */
        /* 页头：连接状态 + Last Updated + 失败/stale（§15/§16/§34）             */
        /* ------------------------------------------------------------------ */
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.smallSpacing

            Components.StatusChip {
                semanticKey: root.controller.engineStateSemanticKey
                iconName: root.controller.engineStateIconName
                text: root.engineStateText(root.controller.engineStateKey)
            }
            QQC2.Label {
                text: "•"
                opacity: 0.5
            }
            QQC2.Label {
                text: Kontainer.Format.isValid(root.controller.lastUpdated)
                    ? i18nc("@info last successful update", "Updated %1 ago", Kontainer.Format.elapsed(root.controller.lastUpdated))
                    : i18n("Not updated yet")
                opacity: 0.75
                font: Kirigami.Theme.smallFont
            }
            QQC2.Label {
                visible: root.controller.updateFailed
                text: i18n("Update failed")
                color: Components.StatusPalette.color("negative")
                font: Kirigami.Theme.smallFont
            }
            QQC2.Label {
                visible: root.controller.stale
                text: i18n("Data is stale")
                color: Components.StatusPalette.color("neutral")
                font: Kirigami.Theme.smallFont
            }
            QQC2.Label {
                // 关闭自动刷新后不给提示的话，用户会以为界面卡住了
                visible: !root.controller.autoRefreshEnabled
                text: i18n("Auto-refresh is off")
                color: Components.StatusPalette.color("neutral")
                font: Kirigami.Theme.smallFont
            }

            Item {
                Layout.fillWidth: true
            }

            QQC2.BusyIndicator {
                // 后台刷新只显示轻量指示，不清空已有内容（§34）
                running: root.controller.busy
                visible: running
                implicitHeight: Kirigami.Units.iconSizes.small
                implicitWidth: implicitHeight
            }
            QQC2.Label {
                text: root.controller.endpoint
                opacity: 0.6
                font: Kirigami.Theme.smallFont
                elide: Text.ElideLeft
                Layout.maximumWidth: Kirigami.Units.gridUnit * 14
            }
        }

        /* ------------------------------------------------------------------ */
        /* 整页错误（只有高频数据集全部失败才会到这里，§30）                     */
        /* ------------------------------------------------------------------ */
        /* ------------------------------------------------------------------ */
        /* 写权限门（ARCH_V4 §2.2.3）：不可写时写入口整体消失，这里说明原因     */
        /* ------------------------------------------------------------------ */
        Kirigami.InlineMessage {
            objectName: "writeAccessBanner"
            Layout.fillWidth: true
            visible: !root.operations.writeAllowed && root.operations.writeAccessText.length > 0
            type: Kirigami.MessageType.Information
            text: root.operations.writeAccessText
        }

        /* 操作结果（成功 / 失败 / 取消）的唯一呈现位置之一 */
        Components.OperationMessage {
            Layout.fillWidth: true
            operations: root.operations
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: root.controller.stateKey === "error"
            type: Kirigami.MessageType.Error
            text: root.controller.engineError.length > 0 ? root.controller.engineError : i18n("Unable to connect to Docker Engine.")
            actions: [
                Kirigami.Action {
                    text: i18n("Retry")
                    icon.name: "view-refresh"
                    onTriggered: root.controller.refresh()
                }
            ]
        }

        /* ------------------------------------------------------------------ */
        /* Overview（统计块 + Storage）                                        */
        /*                                                                     */
        /* 概览区在窗口较矮时不能把列表挤没：给它一个上限高度并允许内部滚动，   */
        /* 列表始终保留最小高度（§19 响应窄窗口 / 小尺寸 KCM 窗口）。           */
        /* ------------------------------------------------------------------ */
        QQC2.ScrollView {
            id: overviewScroll

            objectName: "overviewScroll"

            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(overviewColumn.implicitHeight, root.height * 0.45)
            Layout.maximumHeight: root.height * 0.45
            clip: true

            ColumnLayout {
                id: overviewColumn

                width: overviewScroll.availableWidth
                spacing: Kirigami.Units.smallSpacing

                /* 统计卡按窗口宽度重排（§1.2）：宽 5 列 / 中 3 列 / 窄 2 列。
                   断点用 gridUnit 表达，不写裸像素。 */
                GridLayout {
                    id: tileGrid

                    objectName: "tileGrid"

                    Layout.fillWidth: true
                    columnSpacing: Kirigami.Units.smallSpacing
                    rowSpacing: Kirigami.Units.smallSpacing

                    columns: {
                        if (tileGrid.width >= Kirigami.Units.gridUnit * 40) {
                            return 5;
                        }
                        if (tileGrid.width >= Kirigami.Units.gridUnit * 26) {
                            return 3;
                        }
                        return 2;
                    }

                    /*  model 必须是**稳定的数值**：root.tiles 是每次数据变化都会重新求值的
                        JS 数组，直接当 model 会让 Repeater 在每次刷新时销毁并重建全部
                        统计块。这类「布局正在算尺寸时条目被销毁」的情况会让 Qt 的布局
                        引擎在 polish 阶段访问已析构的条目（实测在 kcmshell6 的
                        QQuickWidget 宿主下会段错误），因此这里按索引取值。 */
                    Repeater {
                        model: root.tiles.length

                        delegate: Components.StatTile {
                            required property int index

                            Layout.fillWidth: true
                            label: root.tiles[index].label
                            value: root.countsReady ? String(root.tiles[index].value) : i18n("—")
                            iconName: root.tiles[index].icon
                            semanticKey: root.tiles[index].semanticKey
                            tintWhenNonZero: true
                            tooltip: root.countsReady ? "" : i18n("Engine summary is unavailable")
                        }
                    }
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    visible: root.controller.engineStateKey === "partial"
                    text: i18n("Container and image counts are unavailable because the engine summary could not be read.")
                    font: Kirigami.Theme.smallFont
                    opacity: 0.7
                    wrapMode: Text.WordWrap
                }

                StorageView {
                    Layout.fillWidth: true
                    controller: root.controller
                    // 看到「数据卷」占用后想看看是哪些：跳到数据卷页（索引 3）
                    onSegmentActivated: function (entryKey) {
                        if (entryKey === "volumes") {
                            tabBar.currentIndex = 3;
                        }
                    }
                }
            }
        }

        /* ------------------------------------------------------------------ */
        /* 标签页                                                             */
        /* ------------------------------------------------------------------ */
        QQC2.TabBar {
            id: tabBar
            objectName: "tabBar"
            Layout.fillWidth: true

            // 网络与数据卷都是低频数据：只在切到对应页面时刷新，不加入 5 秒轮询。
            // 索引：0 容器 / 1 镜像 / 2 网络 / 3 数据卷 / 4 引擎
            onCurrentIndexChanged: {
                if (tabBar.currentIndex === 2) {
                    root.controller.refreshNetworks();
                } else if (tabBar.currentIndex === 3) {
                    root.controller.refreshVolumes();
                }
            }

            QQC2.TabButton {
                text: i18ncp("@title:tab container list", "Containers (%1)", "Containers (%1)", root.controller.containers.count)
            }
            QQC2.TabButton {
                text: i18ncp("@title:tab image list", "Images (%1)", "Images (%1)", root.controller.images.count)
            }
            QQC2.TabButton {
                text: i18ncp("@title:tab network list", "Networks (%1)", "Networks (%1)", root.controller.networkModel.count)
            }
            QQC2.TabButton {
                text: i18ncp("@title:tab volume list", "Volumes (%1)", "Volumes (%1)", root.controller.volumeModel.count)
            }
            QQC2.TabButton {
                text: i18nc("@title:tab engine information", "Engine")
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        /* ------------------------------------------------------------------ */
        /* 工具栏：搜索 / 过滤 / 排序（§9/§10）                                 */
        /* ------------------------------------------------------------------ */
        RowLayout {
            Layout.fillWidth: true
            // 容器/镜像共用的搜索过滤行：网络页与引擎页各有自己的工具栏（索引见 tabBar）
            visible: tabBar.currentIndex === 0 || tabBar.currentIndex === 1
            spacing: Kirigami.Units.smallSpacing

            Kirigami.SearchField {
                id: searchField

                Layout.fillWidth: true
                placeholderText: tabBar.currentIndex === 0 ? i18n("Search containers (name, image, ID)…") : i18n("Search images (repository, tag, ID)…")
                // 条件保存在 proxy 里：后台刷新不会重置（§32）
                text: tabBar.currentIndex === 0 ? root.containerList.searchText : root.imageList.searchText
                onTextEdited: {
                    if (tabBar.currentIndex === 0) {
                        root.containerList.searchText = text;
                    } else {
                        root.imageList.searchText = text;
                    }
                }
            }

            QQC2.ComboBox {
                id: filterBox

                readonly property var containerFilters: [
                    {
                        key: "all",
                        label: i18n("All states")
                    },
                    {
                        key: "running",
                        label: i18n("Running")
                    },
                    {
                        key: "paused",
                        label: i18n("Paused")
                    },
                    {
                        key: "stopped",
                        label: i18n("Stopped")
                    },
                    {
                        key: "restarting",
                        label: i18n("Restarting")
                    },
                    {
                        key: "dead",
                        label: i18n("Dead")
                    }
                ]
                readonly property var imageFilters: [
                    {
                        key: "all",
                        label: i18n("All images")
                    },
                    {
                        key: "in-use",
                        label: i18n("In use")
                    },
                    {
                        key: "dangling",
                        label: i18n("Dangling")
                    }
                ]

                model: tabBar.currentIndex === 0 ? containerFilters : imageFilters
                textRole: "label"
                valueRole: "key"
                onActivated: {
                    if (tabBar.currentIndex === 0) {
                        root.containerList.stateFilter = currentValue;
                    } else {
                        root.imageList.useFilter = currentValue;
                    }
                }
                Component.onCompleted: currentIndex = indexOfValue(tabBar.currentIndex === 0 ? root.containerList.stateFilter : root.imageList.useFilter)
                onModelChanged: currentIndex = indexOfValue(tabBar.currentIndex === 0 ? root.containerList.stateFilter : root.imageList.useFilter)
            }

            QQC2.ComboBox {
                id: sortBox

                readonly property var containerSorts: [
                    {
                        key: "name",
                        label: i18n("Sort: Name")
                    },
                    {
                        key: "state",
                        label: i18n("Sort: State")
                    },
                    {
                        key: "created",
                        label: i18n("Sort: Created")
                    }
                ]
                readonly property var imageSorts: [
                    {
                        key: "repository",
                        label: i18n("Sort: Repository")
                    },
                    {
                        key: "created",
                        label: i18n("Sort: Created")
                    },
                    {
                        key: "size",
                        label: i18n("Sort: Size")
                    }
                ]

                model: tabBar.currentIndex === 0 ? containerSorts : imageSorts
                textRole: "label"
                valueRole: "key"
                onActivated: {
                    if (tabBar.currentIndex === 0) {
                        root.containerList.sortKey = currentValue;
                    } else {
                        root.imageList.sortKey = currentValue;
                    }
                }
                Component.onCompleted: currentIndex = indexOfValue(tabBar.currentIndex === 0 ? root.containerList.sortKey : root.imageList.sortKey)
                onModelChanged: currentIndex = indexOfValue(tabBar.currentIndex === 0 ? root.containerList.sortKey : root.imageList.sortKey)
            }

            // 构建镜像（八期 §5.4）：只读模式不出现入口
            QQC2.Button {
                objectName: "buildImageEntryButton"
                visible: tabBar.currentIndex === 1 && root.operations.writeAllowed
                text: i18n("Build image…")
                icon.name: "run-build"
                onClicked: root.buildPanelOpen = !root.buildPanelOpen
            }

            // 创建容器（七期 §4.4）：只读模式不出现入口
            QQC2.Button {
                objectName: "createContainerEntryButton"
                visible: tabBar.currentIndex === 0 && root.operations.writeAllowed
                text: i18n("Create container…")
                icon.name: "list-add"
                onClicked: root.createContainerRequested("")
            }

            // 仓库登录（ARCH_V5_V8 §2.7）：私有仓库拉取前先登录
            QQC2.Button {
                objectName: "registryAuthEntryButton"
                visible: tabBar.currentIndex === 1
                text: i18n("Registry logins…")
                icon.name: "dialog-password"
                onClicked: root.registryAuthRequested("")
            }

            // 拉取镜像（ARCH_V4 §2.4）：唯一会新增镜像的入口。
            // 权限门不允许写时按钮整体不出现，而不是禁用后静默。
            QQC2.Button {
                objectName: "pullImageEntryButton"
                visible: tabBar.currentIndex === 1 && root.operations.writeAllowed
                text: root.operations.activePullCount > 0
                    ? i18n("Pull image (%1 running)", root.operations.activePullCount)
                    : i18n("Pull image")
                icon.name: "download"
                onClicked: {
                    pullDialog.reset();
                    pullDialog.open();
                }
            }
        }

        /* ------------------------------------------------------------------ */
        /* 内容区                                                             */
        /* ------------------------------------------------------------------ */
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: Kirigami.Units.gridUnit * 8
            currentIndex: tabBar.currentIndex

            /* ---------------------------- 容器 ---------------------------- */
            ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    visible: root.controller.containersStateKey === "error"
                    type: Kirigami.MessageType.Error
                    text: i18n("Unable to retrieve the container list: %1", root.controller.containersError)
                }

                Components.EmptyPlaceholder {
                    objectName: "containersEmptyPlaceholder"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    message: root.containersEmptyState.message
                    actionText: root.containersEmptyState.actionText
                    actionIconName: "edit-clear"
                    onActionTriggered: root.handleEmptyAction()
                }

                ListView {
                    id: containerView
                    objectName: "containerView"

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.containersEmptyState.message.length === 0
                    clip: true
                    // 后台刷新时保留旧数据与滚动位置（§32/§34）
                    property real savedContentY: 0
                    model: root.containerList
                    spacing: Kirigami.Units.smallSpacing / 2
                    // 键盘导航（§38）
                    keyNavigationEnabled: true
                    activeFocusOnTab: true

                    QQC2.ScrollBar.vertical: QQC2.ScrollBar {}

                    // 数据确实发生变化时模型会重置：把滚动位置恢复回去
                    Connections {
                        target: containerView.model

                        function onModelAboutToBeReset() {
                            containerView.savedContentY = containerView.contentY;
                        }
                        function onModelReset() {
                            containerView.contentY = containerView.savedContentY;
                        }
                    }

                    delegate: ContainerCard {
                        operations: root.operations
                        onActivated: root.containerActivated(containerId)
                    }
                }
            }

            /* ---------------------------- 镜像 ---------------------------- */
            ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    visible: root.controller.imagesStateKey === "error"
                    type: Kirigami.MessageType.Error
                    text: i18n("Unable to retrieve the image list: %1", root.controller.imagesError)
                }

                /* 拉取进度（可并发、后台继续、失败保留原因，ARCH_V4 §2.4） */
                Components.BuildImagePanel {
                    id: buildPanel

                    Layout.fillWidth: true
                    operations: root.operations
                    formOpen: root.buildPanelOpen
                    buildCacheBytes: root.controller.storage.buildCacheBytes > 0 ? root.controller.storage.buildCacheBytes : 0
                    visible: root.buildPanelOpen || root.operations.builds.count > 0
                    onImageRequested: function (imageId) {
                        root.imageActivated(imageId);
                    }
                }

                Components.PullProgressList {
                    Layout.fillWidth: true
                    operations: root.operations
                    // 401/403 的失败：直接把人带到对应仓库的登录框，而不是让他自己找入口
                    onLoginRequested: function (reference) {
                        root.registryAuthRequested(root.controller.registryAuth.serverAddressForImage(reference));
                    }
                }

                Components.EmptyPlaceholder {
                    objectName: "imagesEmptyPlaceholder"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    message: root.imagesEmptyState.message
                    actionText: root.imagesEmptyState.actionText
                    actionIconName: "edit-clear"
                    onActionTriggered: root.handleEmptyAction()
                }

                ListView {
                    id: imageView
                    objectName: "imageView"

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.imagesEmptyState.message.length === 0
                    clip: true
                    property real savedContentY: 0
                    model: root.imageList
                    spacing: Kirigami.Units.smallSpacing / 2
                    keyNavigationEnabled: true
                    activeFocusOnTab: true

                    QQC2.ScrollBar.vertical: QQC2.ScrollBar {}

                    Connections {
                        target: imageView.model

                        function onModelAboutToBeReset() {
                            imageView.savedContentY = imageView.contentY;
                        }
                        function onModelReset() {
                            imageView.contentY = imageView.savedContentY;
                        }
                    }

                    delegate: ImageCard {
                        onActivated: root.imageActivated(imageId)
                    }
                }
            }

            /* ---------------------------- 网络 ---------------------------- */
            ColumnLayout {
                id: networksTab

                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Kirigami.Units.smallSpacing

                Kirigami.InlineMessage {
                    objectName: "networksErrorMessage"
                    Layout.fillWidth: true
                    visible: root.controller.networksStateKey === "error"
                    type: Kirigami.MessageType.Error
                    text: i18n("Unable to retrieve the network list: %1", root.controller.networksError)
                }

                /* 搜索 / 过滤 / 排序：与容器、镜像列表同一套交互 */
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.TextField {
                        objectName: "networkSearchField"
                        Layout.fillWidth: true
                        placeholderText: i18n("Search by name, ID, driver or subnet…")
                        text: root.networkList.searchText
                        onTextChanged: root.networkList.searchText = text
                    }

                    // 创建网络（六期 §3.3）：只读模式不出现入口，而不是禁用后静默
                    QQC2.Button {
                        objectName: "createNetworkEntryButton"
                        visible: root.operations.writeAllowed
                        text: i18n("Create network…")
                        icon.name: "list-add"
                        onClicked: {
                            createNetworkDialog.reset();
                            createNetworkDialog.open();
                        }
                    }

                    QQC2.ComboBox {
                        id: networkOriginCombo

                        objectName: "networkOriginCombo"
                        textRole: "text"
                        valueRole: "value"
                        model: [
                            {text: i18n("All networks"), value: "all"},
                            {text: i18n("Built-in"), value: "predefined"},
                            {text: i18n("User-defined"), value: "custom"}
                        ]
                        onActivated: root.networkList.originFilter = currentValue
                        Component.onCompleted: currentIndex = indexOfValue(root.networkList.originFilter)
                    }

                    QQC2.ComboBox {
                        id: networkSortCombo

                        objectName: "networkSortCombo"
                        textRole: "text"
                        valueRole: "value"
                        model: [
                            {text: i18n("Name"), value: "name"},
                            {text: i18n("Driver"), value: "driver"},
                            {text: i18n("Scope"), value: "scope"},
                            {text: i18n("Containers"), value: "members"}
                        ]
                        onActivated: root.networkList.sortKey = currentValue
                        Component.onCompleted: currentIndex = indexOfValue(root.networkList.sortKey)
                    }
                }

                Components.EmptyPlaceholder {
                    objectName: "networksEmptyPlaceholder"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    message: root.networkList.count === 0 && root.controller.networkModel.count > 0
                        ? i18n("No network matches the current search or filter.")
                        : root.controller.networkModel.count === 0 ? i18n("No networks found.") : ""
                    explanationText: root.networkList.count === 0 && root.controller.networkModel.count > 0
                        ? i18n("Clear the search field or switch the filter back to “All networks”.")
                        : root.controller.networkModel.count === 0
                            ? i18n("Docker always provides at least the built-in bridge, host and none networks.")
                            : ""
                    actionText: root.networkList.count === 0 && root.controller.networkModel.count > 0 ? i18n("Clear filters") : ""
                    actionIconName: "edit-clear"
                    onActionTriggered: {
                        root.networkList.searchText = "";
                        root.networkList.originFilter = "all";
                        networkOriginCombo.currentIndex = networkOriginCombo.indexOfValue("all");
                        networkSearchField.text = "";
                    }
                }

                ListView {
                    id: networkView

                    objectName: "networkView"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.networkList.count > 0
                    clip: true
                    model: root.networkList
                    spacing: Kirigami.Units.smallSpacing / 2
                    keyNavigationEnabled: true
                    activeFocusOnTab: true

                    QQC2.ScrollBar.vertical: QQC2.ScrollBar {}

                    delegate: NetworkCard {
                        onActivated: root.networkActivated(id)
                    }
                }
            }

            /* ---------------------------- 数据卷 --------------------------- */
            ColumnLayout {
                id: volumesTab

                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Kirigami.Units.smallSpacing

                Kirigami.InlineMessage {
                    objectName: "volumesErrorMessage"
                    Layout.fillWidth: true
                    visible: root.controller.volumesStateKey === "error"
                    type: Kirigami.MessageType.Error
                    text: i18n("Unable to retrieve the volume list: %1", root.controller.volumesError)
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.TextField {
                        objectName: "volumeSearchField"
                        Layout.fillWidth: true
                        placeholderText: i18n("Search by name, driver or mount point…")
                        text: root.volumeList.searchText
                        onTextChanged: root.volumeList.searchText = text
                    }

                    QQC2.ComboBox {
                        id: volumeUsageCombo

                        objectName: "volumeUsageCombo"
                        textRole: "text"
                        valueRole: "value"
                        model: [
                            {text: i18n("All volumes"), value: "all"},
                            {text: i18n("Unused"), value: "unused"},
                            {text: i18n("In use"), value: "inUse"}
                        ]
                        onActivated: root.volumeList.usageFilter = currentValue
                        Component.onCompleted: currentIndex = indexOfValue(root.volumeList.usageFilter)
                    }

                    QQC2.ComboBox {
                        id: volumeSortCombo

                        objectName: "volumeSortCombo"
                        textRole: "text"
                        valueRole: "value"
                        model: [
                            {text: i18n("Name"), value: "name"},
                            {text: i18n("Driver"), value: "driver"},
                            {text: i18n("Size"), value: "size"},
                            {text: i18n("Containers"), value: "refs"}
                        ]
                        onActivated: root.volumeList.sortKey = currentValue
                        Component.onCompleted: currentIndex = indexOfValue(root.volumeList.sortKey)
                    }

                    // 创建卷（只读模式不出现）
                    QQC2.Button {
                        objectName: "createVolumeEntryButton"
                        visible: root.operations.writeAllowed
                        text: i18n("Create volume…")
                        icon.name: "list-add"
                        onClicked: root.volumeCreatePanelOpen = !root.volumeCreatePanelOpen
                    }

                    // 清理未使用（先列出将被删除的卷，再确认）
                    QQC2.Button {
                        objectName: "pruneVolumesEntryButton"
                        visible: root.operations.writeAllowed
                        text: i18n("Clean up unused…")
                        icon.name: "edit-clear"
                        // 先读一次 count 建立依赖：QML 不追踪函数调用，否则模型填充后
                        // 这个 enabled 会停留在初始值（按钮一直是灰的）
                        enabled: root.controller.volumeModel.count >= 0
                            && root.controller.volumeModel.unusedNames().length > 0
                        onClicked: root.volumePrunePanelOpen = !root.volumePrunePanelOpen
                    }
                }

                /* 创建卷：内联面板（理由同"连接网络"：弹层内容在离屏时序下不可靠） */
                ColumnLayout {
                    objectName: "volumeCreatePanel"
                    Layout.fillWidth: true
                    visible: root.volumeCreatePanelOpen && root.operations.writeAllowed
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.InlineMessage {
                        objectName: "volumeCreateError"
                        Layout.fillWidth: true
                        visible: root.operations.resultKey === "error" && root.operations.resultText.length > 0
                        type: Kirigami.MessageType.Error
                        text: root.operations.resultText
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        QQC2.TextField {
                            id: volumeNameField

                            objectName: "volumeNameField"
                            Layout.fillWidth: true
                            placeholderText: i18n("Volume name")
                            Accessible.name: i18n("Volume name")
                        }

                        QQC2.Button {
                            objectName: "createVolumeButton"
                            text: i18n("Create")
                            icon.name: "list-add"
                            enabled: volumeNameField.text.trim().length > 0
                            onClicked: {
                                if (root.operations.createVolume(volumeNameField.text, "local", [])) {
                                    root.volumeCreatePanelOpen = false;
                                    volumeNameField.text = "";
                                }
                            }
                        }

                        QQC2.Label {
                            Layout.fillWidth: true
                            text: i18n("The volume is created with the local driver.")
                            font: Kirigami.Theme.smallFont
                            opacity: 0.75
                            elide: Text.ElideRight
                        }
                    }
                }

                /* 清理未使用：**先列出将被删除的卷**与可回收空间，再让用户确认 */
                ColumnLayout {
                    objectName: "volumePrunePanel"
                    Layout.fillWidth: true
                    visible: root.volumePrunePanelOpen && root.operations.writeAllowed
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Separator {
                        Layout.fillWidth: true
                    }

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18ncp("@info volumes about to be removed", "One unused volume will be removed:", "%1 unused volumes will be removed:", root.controller.volumeModel.unusedNames().length)
                        font.bold: true
                    }

                    Repeater {
                        model: root.controller.volumeModel.unusedNames()

                        delegate: QQC2.Label {
                            required property string modelData
                            Layout.fillWidth: true
                            text: "• " + modelData
                            font.family: "monospace"
                            elide: Text.ElideMiddle
                        }
                    }

                    QQC2.Label {
                        objectName: "volumePruneSpaceLabel"
                        Layout.fillWidth: true
                        text: i18n("Reclaimable space: %1", root.pruneReclaimableText())
                        opacity: 0.8
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        QQC2.Button {
                            objectName: "pruneVolumesConfirmButton"
                            text: i18n("Clean up")
                            icon.name: "edit-clear"
                            onClicked: {
                                root.operations.pruneVolumes();
                                root.volumePrunePanelOpen = false;
                            }
                        }

                        QQC2.Label {
                            Layout.fillWidth: true
                            text: i18n("Only volumes that no container uses are removed.")
                            font: Kirigami.Theme.smallFont
                            opacity: 0.75
                            elide: Text.ElideRight
                        }
                    }
                }

                Components.EmptyPlaceholder {
                    objectName: "volumesEmptyPlaceholder"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    message: root.volumeList.count === 0 && root.controller.volumeModel.count > 0
                        ? i18n("No volume matches the current search or filter.")
                        : root.controller.volumeModel.count === 0 ? i18n("No volumes found.") : ""
                    explanationText: root.volumeList.count === 0 && root.controller.volumeModel.count > 0
                        ? i18n("Clear the search field or switch the filter back to “All volumes”.")
                        : root.controller.volumeModel.count === 0
                            ? i18n("Volumes keep data across container restarts. Create one here or let Docker create it when a container declares it.")
                            : ""
                    actionText: root.volumeList.count === 0 && root.controller.volumeModel.count > 0 ? i18n("Clear filters") : ""
                    actionIconName: "edit-clear"
                    onActionTriggered: {
                        root.volumeList.searchText = "";
                        root.volumeList.usageFilter = "all";
                        volumeUsageCombo.currentIndex = volumeUsageCombo.indexOfValue("all");
                        volumeSearchField.text = "";
                    }
                }

                ListView {
                    id: volumeView

                    objectName: "volumeView"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.volumeList.count > 0
                    clip: true
                    model: root.volumeList
                    spacing: Kirigami.Units.smallSpacing / 2
                    keyNavigationEnabled: true
                    activeFocusOnTab: true

                    QQC2.ScrollBar.vertical: QQC2.ScrollBar {}

                    delegate: VolumeCard {
                        onActivated: root.volumeActivated(name)
                    }
                }
            }

            /* ---------------------------- Engine --------------------------- */
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Kirigami.Units.smallSpacing

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.Button {
                        objectName: "openUserRuntimeConfigButton"
                        text: i18n("User configuration…")
                        icon.name: "settings-configure"
                        onClicked: root.configureRuntimeRequested("user")
                    }

                    QQC2.Button {
                        objectName: "openSystemRuntimeConfigButton"
                        text: i18n("System configuration…")
                        icon.name: "settings-configure"
                        onClicked: root.configureRuntimeRequested("system")
                    }

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("Registry mirrors and other daemon settings. The system configuration needs administrator rights.")
                        font: Kirigami.Theme.smallFont
                        opacity: 0.7
                        elide: Text.ElideRight
                    }
                }

                QQC2.ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    EngineStatusView {
                        width: parent.width
                        engine: root.controller.engine
                        buildStamp: root.controller.buildStamp
                    }
                }
            }
        }
    }

    /* 拉取镜像对话框（进度与取消也在这里） */
    Components.CreateNetworkDialog {
        id: createNetworkDialog
        operations: root.operations
    }

    Components.PullImageDialog {
        id: pullDialog
        operations: root.operations
        // 该仓库已有凭据（或引用还没填）：不提示"需要登录"；
        // 没有凭据时才给出「去登录…」引导
        credentialKnown: pullDialog.referenceInput.serverAddress === ""
            || root.controller.registryAuth.hasCredentialForImage(pullDialog.referenceInput.text)
        onPullRequested: function (reference) {
            root.operations.pullImage(reference);
        }
        onLoginRequested: function (serverAddress) {
            root.registryAuthRequested(serverAddress);
        }
    }
}
