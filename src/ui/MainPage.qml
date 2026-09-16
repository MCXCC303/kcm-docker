/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Kontainer 首页（ARCH_V2 §5）：Engine 状态 + Overview + Storage + 容器/镜像列表。

    由 main.qml 放进 StackView 作为根页面；卡片激活时发出信号，由 main.qml 负责导航。
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

    /*! Overview 统计块（纯展示层聚合） */
    readonly property bool countsReady: controller.engine.countsAvailable
    readonly property var tiles: [
        {
            label: i18n("Containers"),
            value: controller.engine.containerTotal,
            icon: "application-x-executable"
        },
        {
            label: i18n("Running"),
            value: controller.engine.containersRunning,
            icon: "media-playback-start",
            accent: Kirigami.Theme.positiveTextColor
        },
        {
            label: i18n("Paused"),
            value: controller.engine.containersPaused,
            icon: "media-playback-pause",
            accent: Kirigami.Theme.neutralTextColor
        },
        {
            label: i18n("Stopped"),
            value: controller.engine.containersStopped,
            icon: "media-playback-stop",
            accent: Kirigami.Theme.negativeTextColor
        },
        {
            label: i18n("Images"),
            value: controller.engine.imageCount,
            icon: "image-x-generic"
        }
    ]

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

    function engineStateIcon(stateKey: string): string {
        switch (stateKey) {
        case "ready":
        case "refreshing":
            return "dialog-ok-apply";
        case "loading":
            return "chronometer";
        default:
            return "dialog-error";
        }
    }

    /* §33：四种空状态必须区分，不能混为一谈 */
    function containersEmptyText(): string {
        if (containerList.count > 0 || controller.containersStateKey === "error") {
            return "";
        }
        if (controller.containers.count === 0) {
            return i18n("No containers found.");
        }
        if (containerList.searchText.length > 0) {
            return i18nc("@info no container matches the search", "No containers match “%1”.", containerList.searchText);
        }
        return i18n("No containers match the current filter.");
    }

    function imagesEmptyText(): string {
        if (imageList.count > 0 || controller.imagesStateKey === "error") {
            return "";
        }
        if (controller.images.count === 0) {
            return i18n("No images found.");
        }
        if (imageList.searchText.length > 0) {
            return i18nc("@info no image matches the search", "No images match “%1”.", imageList.searchText);
        }
        return i18n("No images match the current filter.");
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

            Kirigami.Icon {
                source: root.engineStateIcon(root.controller.engineStateKey)
                color: {
                    switch (root.controller.engineStateKey) {
                    case "ready":
                    case "refreshing":
                        return Kirigami.Theme.positiveTextColor;
                    case "loading":
                        return Kirigami.Theme.textColor;
                    default:
                        return Kirigami.Theme.negativeTextColor;
                    }
                }
                implicitWidth: Kirigami.Units.iconSizes.small
                implicitHeight: Kirigami.Units.iconSizes.small
            }
            QQC2.Label {
                text: root.engineStateText(root.controller.engineStateKey)
                font.bold: true
                color: {
                    switch (root.controller.engineStateKey) {
                    case "ready":
                    case "refreshing":
                        return Kirigami.Theme.positiveTextColor;
                    case "loading":
                        return Kirigami.Theme.textColor;
                    default:
                        return Kirigami.Theme.negativeTextColor;
                    }
                }
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
                color: Kirigami.Theme.negativeTextColor
                font: Kirigami.Theme.smallFont
            }
            QQC2.Label {
                visible: root.controller.stale
                text: i18n("Data is stale")
                color: Kirigami.Theme.negativeTextColor
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

            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(overviewColumn.implicitHeight, root.height * 0.45)
            Layout.maximumHeight: root.height * 0.45
            clip: true

            ColumnLayout {
                id: overviewColumn

                width: overviewScroll.availableWidth
                spacing: Kirigami.Units.smallSpacing

                Flow {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    Repeater {
                        model: root.tiles

                        delegate: Components.StatTile {
                            required property var modelData

                            label: modelData.label
                            value: root.countsReady ? String(modelData.value) : i18n("—")
                            iconName: modelData.icon
                            accent: modelData.accent !== undefined ? modelData.accent : Kirigami.Theme.textColor
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

                QQC2.Label {
                    Layout.fillWidth: true
                    visible: root.containersEmptyText().length > 0
                    text: root.containersEmptyText()
                    opacity: 0.7
                    wrapMode: Text.WordWrap
                }

                ListView {
                    id: containerView
                    objectName: "containerView"

                    Layout.fillWidth: true
                    Layout.fillHeight: true
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

                QQC2.Label {
                    Layout.fillWidth: true
                    visible: root.imagesEmptyText().length > 0
                    text: root.imagesEmptyText()
                    opacity: 0.7
                    wrapMode: Text.WordWrap
                }

                ListView {
                    id: imageView
                    objectName: "imageView"

                    Layout.fillWidth: true
                    Layout.fillHeight: true
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
