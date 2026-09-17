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

    /*! 卡片被激活：由 main.qml 接到导航上（ARCH_V2 §43：导航属于 KCM 层） */
    signal containerActivated(string containerId)
    signal imageActivated(string imageId)

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
                actionText: ""
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

            QQC2.TabButton {
                text: i18ncp("@title:tab container list", "Containers (%1)", "Containers (%1)", root.controller.containers.count)
            }
            QQC2.TabButton {
                text: i18ncp("@title:tab image list", "Images (%1)", "Images (%1)", root.controller.images.count)
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
            visible: tabBar.currentIndex !== 2
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
                    onActionTriggered: root.clearCurrentTabFilters()
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

                Components.EmptyPlaceholder {
                    objectName: "imagesEmptyPlaceholder"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    message: root.imagesEmptyState.message
                    actionText: root.imagesEmptyState.actionText
                    actionIconName: "edit-clear"
                    onActionTriggered: root.clearCurrentTabFilters()
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

            /* ---------------------------- Engine --------------------------- */
            QQC2.ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true

                EngineStatusView {
                    width: parent.width
                    engine: root.controller.engine
                }
            }
        }
    }
}
