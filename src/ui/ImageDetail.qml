/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Image Detail（ARCH_V2 §8/§52 / ARCH_V3 §2.3）：

    Repository / Tag / 完整引用 / ID / Digest / Size / Architecture / OS /
    Layers / 使用该镜像的容器（只读关联）/ Environment（默认折叠）。

    三期的两处收敛（§2.3）：
    - 层次默认只显示前 5 层，可展开全部（层数可达数十，全铺会把页面撑得很长）
    - 多 tag 用 chip 呈现，不再逐行占高

    详情内容明显高于窗口：仍然用可滚动页面（Kirigami.Page 不提供滚动），
    正文限宽居中避免宽窗口下一行过长（§1.2）。
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kcmutils as KCM
import org.kde.kirigami as Kirigami
import org.kde.kontainer as Kontainer

import "components" as Components

KCM.SimpleKCM {
    id: page

    property string imageId: ""

    readonly property var controller: kcm.controller.imageDetail
    /*! 写操作控制器（ARCH_V4 §2.4）。 */
    readonly property var operations: kcm.controller.operations
    readonly property bool ready: controller.loadStateKey === "ready"

    /*! 折叠时显示的层数（§2.3）。 */
    readonly property int collapsedLayerCount: 5
    property bool layersExpanded: false

    /*! 请求返回列表页（由 main.qml 接 StackView.pop）。
        注意：不能叫 backRequested——Kirigami.Page 已经声明了同名信号。 */
    signal closeRequested

    /*!
        删除语义（ARCH_V4 §2.4）：
        - 有标签 → 按「仓库:标签」删除，只移除该标签，其他标签保留
        - 有多个标签 → 额外提供「删除全部标签」，走 force=true
        - 无标签（dangling）→ 按 ID 删除
        被容器引用时引擎返回 409，文案会说明原因，不自动 force。
    */
    readonly property bool targetBusy: {
        // 同上：函数调用本身不建立依赖，必须先读 stateRevision
        page.operations.stateRevision;
        return page.operations.isImageBusy(page.imageId);
    }
    readonly property bool canRemove: page.ready && !page.targetBusy && page.operations.writeAllowed
    readonly property bool hasTags: controller.primaryTag.length > 0
    readonly property bool multipleTags: controller.tags.count > 1

    /*! 正文最大宽度：约 42 gridUnit，避免宽窗口下一行过长（§1.2）。 */
    readonly property real contentMaxWidth: Kirigami.Units.gridUnit * 42

    Component.onCompleted: {
        // 二次进入同一镜像也要重新加载
        if (page.imageId.length > 0) {
            controller.imageId = page.imageId;
            controller.start();
        }
        // 层列表默认折叠：只让 model 暴露前 N 条（§2.3）
        controller.layers.limit = page.collapsedLayerCount;
    }

    Component.onDestruction: controller.stop()

    function toggleLayers() {
        page.layersExpanded = !page.layersExpanded;
        // 0 = 不限制
        controller.layers.limit = page.layersExpanded ? 0 : page.collapsedLayerCount;
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        /* 本页触发的操作结果（拉取 / 删除）在这里呈现 */
        Components.OperationMessage {
            Layout.fillWidth: true
            operations: page.operations
        }

        /* ------------------------------------------------------------------ */
        /* 页头：返回 + 镜像名（与容器详情保持一致，用户始终知道自己在看哪个镜像）  */
        /* ------------------------------------------------------------------ */
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            QQC2.Button {
                text: i18n("Images")
                icon.name: "go-previous"
                onClicked: page.closeRequested()
            }
            Kirigami.Heading {
                Layout.fillWidth: true
                level: 2
                elide: Text.ElideMiddle
                text: controller.primaryTag.length > 0 ? controller.primaryTag : i18n("Image")
            }
            Components.CopyButton {
                value: controller.primaryTag
                fieldLabel: i18n("image reference")
            }
        }

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
                text: i18n("Loading image details…")
                opacity: 0.7
            }
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: controller.loadStateKey === "error"
            type: Kirigami.MessageType.Error
            text: controller.errorText.length > 0 ? controller.errorText : i18n("Unable to retrieve image details.")
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

        /* ------------------------------------------------------------------ */
        /* 正文：限宽居中（§1.2）                                                */
        /* ------------------------------------------------------------------ */
        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: page.contentMaxWidth
            visible: page.ready
            spacing: Kirigami.Units.largeSpacing

            /* ---------------- Overview ---------------- */
            Kirigami.FormLayout {
                Layout.fillWidth: true

                Components.CopyableText {
                    Kirigami.FormData.label: i18n("Repository:")
                    value: controller.primaryRepository
                    placeholderText: i18n("<none> (dangling)")
                    fieldLabel: i18n("repository")
                }

                Components.CopyableText {
                    Kirigami.FormData.label: i18n("Tag:")
                    value: controller.tagName
                    placeholderText: i18n("<none>")
                    fieldLabel: i18n("tag")
                }

                Components.CopyableText {
                    Kirigami.FormData.label: i18n("Full reference:")
                    value: controller.primaryTag
                    placeholderText: i18n("<none>")
                    fieldLabel: i18n("image reference")
                }

                Components.CopyableText {
                    Kirigami.FormData.label: i18n("Image ID:")
                    value: controller.shortId
                    copyValue: controller.imageId
                    fieldLabel: i18n("image ID")
                }

                QQC2.Label {
                    Kirigami.FormData.label: i18n("Created:")
                    visible: Kontainer.Format.isValid(controller.created)
                    text: i18nc("@info absolute time and relative", "%1 (%2 ago)", Kontainer.Format.absoluteTime(controller.created), Kontainer.Format.elapsed(controller.created))
                }
                QQC2.Label {
                    Kirigami.FormData.label: i18n("Size:")
                    text: Kontainer.Format.byteSize(controller.sizeBytes)
                }
                QQC2.Label {
                    Kirigami.FormData.label: i18n("Architecture:")
                    visible: text.length > 0
                    text: controller.variant.length > 0 ? controller.architecture + "/" + controller.variant : controller.architecture
                }
                QQC2.Label {
                    Kirigami.FormData.label: i18n("OS:")
                    visible: text.length > 0
                    text: controller.os
                }
                QQC2.Label {
                    Kirigami.FormData.label: i18n("Author:")
                    visible: text.length > 0
                    text: controller.author
                }
            }

            /* ---------------- Tags（chip 呈现，§2.3） ---------------- */
            Kirigami.Heading {
                level: 3
                visible: controller.tags.count > 0
                text: i18n("Tags")
            }
            Kirigami.Separator {
                Layout.fillWidth: true
                visible: controller.tags.count > 0
            }

            Flow {
                Layout.fillWidth: true
                visible: controller.tags.count > 0
                spacing: Kirigami.Units.smallSpacing

                Repeater {
                    model: controller.tags

                    delegate: Kirigami.Badge {
                        required property string label

                        text: label
                        // tag 只是展示，不可点击删除（三期没有写操作）
                        Accessible.role: Accessible.StaticText
                        Accessible.name: label
                    }
                }
            }

            /* ---------------- Digests ---------------- */
            Kirigami.Heading {
                level: 3
                visible: controller.digests.count > 0
                text: i18n("Digests")
            }
            Kirigami.Separator {
                Layout.fillWidth: true
                visible: controller.digests.count > 0
            }

            ColumnLayout {
                Layout.fillWidth: true
                visible: controller.digests.count > 0
                spacing: 0

                Repeater {
                    model: controller.digests

                    delegate: ColumnLayout {
                        required property string label
                        required property string value

                        Layout.fillWidth: true
                        spacing: 0

                        QQC2.Label {
                            Layout.fillWidth: true
                            text: label
                            font.family: "monospace"
                            elide: Text.ElideMiddle
                        }
                        QQC2.Label {
                            Layout.fillWidth: true
                            text: value
                            // 不能先整体赋值 font 再赋值 font.family（QML 会报 Property has already been assigned）
                            font.family: "monospace"
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                            opacity: 0.6
                            elide: Text.ElideMiddle
                        }
                    }
                }
            }

            /* ---------------- Layers（默认折叠前 5 层，§2.3） ---------------- */
            Kirigami.Heading {
                level: 3
                text: i18ncp("@info image layer count", "Layers (%1)", "Layers (%1)", controller.layerCount)
            }
            Kirigami.Separator {
                Layout.fillWidth: true
            }

            QQC2.Label {
                Layout.fillWidth: true
                text: i18n("The Docker API reports layer digests, not per-layer sizes.")
                font: Kirigami.Theme.smallFont
                opacity: 0.6
                wrapMode: Text.WordWrap
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                Repeater {
                    model: controller.layers

                    delegate: RowLayout {
                        required property string label
                        required property string value

                        objectName: "layerEntry"
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        QQC2.Label {
                            text: label
                            Layout.preferredWidth: Kirigami.Units.gridUnit * 2
                            opacity: 0.7
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

            QQC2.Button {
                Layout.alignment: Qt.AlignHCenter
                visible: controller.layerCount > page.collapsedLayerCount
                text: page.layersExpanded ? i18n("Show fewer layers") : i18ncp("@info show all image layers", "Show all %1 layers", "Show all %1 layers", controller.layerCount)
                icon.name: page.layersExpanded ? "arrow-up" : "arrow-down"
                onClicked: page.toggleLayers()
            }

            /* ---------------- Containers using this image（只读关联，§52） ---------------- */
            Kirigami.Heading {
                level: 3
                text: i18n("Containers")
            }
            Kirigami.Separator {
                Layout.fillWidth: true
            }

            Components.EmptyPlaceholder {
                Layout.fillWidth: true
                message: controller.usedByContainers.empty ? i18n("No containers use this image.") : ""
            }

            ColumnLayout {
                Layout.fillWidth: true
                visible: !controller.usedByContainers.empty
                spacing: 0

                Repeater {
                    model: controller.usedByContainers

                    delegate: RowLayout {
                        required property string label
                        required property string value
                        required property string entryKey

                        objectName: "usedByEntry"
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        Kirigami.Icon {
                            source: Kontainer.Presentation.stateIconName(entryKey)
                            color: Components.StatusPalette.color(Kontainer.Presentation.stateSemanticKey(entryKey, "none"))
                            implicitWidth: Kirigami.Units.iconSizes.small
                            implicitHeight: Kirigami.Units.iconSizes.small
                        }
                        QQC2.Label {
                            text: label
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                        QQC2.Label {
                            text: value
                            font: Kirigami.Theme.smallFont
                            opacity: 0.8
                        }
                    }
                }
            }

            /* ---------------- Environment（默认折叠，§40） ---------------- */
            Components.CollapsibleSection {
                Layout.fillWidth: true
                contentObjectName: "imageEnvironmentValues"
                title: i18ncp("@info environment variable count", "Environment (%1 variable)", "Environment (%1 variables)", controller.environmentCount)

                // 镜像的 environment 是字符串列表（"KEY=value"），不是键值模型
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    /* 同 MainPage：model 用长度而不是 QStringList 属性本身，
                       避免列表变化时在布局 polish 期间重建条目。 */
                    Repeater {
                        model: controller.environment.length

                        delegate: QQC2.Label {
                            required property int index

                            Layout.fillWidth: true
                            text: controller.environment[index]
                            font.family: "monospace"
                            elide: Text.ElideMiddle
                        }
                    }
                }
            }
        }
    }

    /*!
        删除入口（ARCH_V4 §2.4）：破坏性操作只在详情页，且必须二次确认。
        权限门不允许写时整条 footer 不出现。
    */
    footer: QQC2.ToolBar {
        id: actionBar

        objectName: "imageActionBar"
        visible: page.operations.writeAllowed
        position: QQC2.ToolBar.Footer

        contentItem: RowLayout {
            spacing: Kirigami.Units.smallSpacing

            QQC2.BusyIndicator {
                objectName: "imageBusyIndicator"
                visible: page.targetBusy
                running: page.targetBusy
                implicitWidth: Kirigami.Units.iconSizes.smallMedium
                implicitHeight: Kirigami.Units.iconSizes.smallMedium
            }

            Item {
                Layout.fillWidth: true
            }

            QQC2.Button {
                objectName: "imageRemoveAllTagsButton"
                visible: page.canRemove && page.multipleTags
                text: i18n("Delete all tags…")
                icon.name: "edit-delete"
                onClicked: removeAllTagsDialog.open()
            }

            QQC2.Button {
                objectName: "imageRemoveButton"
                visible: page.canRemove
                text: i18n("Delete")
                icon.name: "edit-delete"
                onClicked: removeDialog.open()
            }
        }
    }

    /* 删除单个标签（默认动作） */
    Components.ConfirmDialog {
        id: removeDialog

        objectName: "removeImageDialog"
        headingText: i18n("Delete image tag")
        questionText: page.hasTags
            ? i18n("Delete the tag “%1”?", controller.primaryTag)
            : i18n("Delete the image “%1”?", controller.shortId)
        consequenceText: i18n("Only this tag is removed. Layers shared with other images are kept.")
        acceptText: i18n("Delete")
        destructive: true
        onConfirmed: {
            if (page.hasTags) {
                page.operations.removeImage(controller.primaryTag, false);
            } else {
                page.operations.removeImage(page.imageId, false);
            }
        }
    }

    /* 删除全部标签：force，且不提供「顺便清理」之类的额外选项 */
    Components.ConfirmDialog {
        id: removeAllTagsDialog

        objectName: "removeAllImageTagsDialog"
        headingText: i18n("Delete all tags")
        questionText: i18n("Delete every tag of this image (%1 tags)?", controller.tags.count)
        consequenceText: i18n("All tags of this image are removed. Containers that reference it can no longer be started.")
        acceptText: i18n("Delete all tags")
        destructive: true
        onConfirmed: page.operations.removeImage(page.imageId, true)
    }

    /* 删除成功后本页目标已不存在：返回列表 */
    Connections {
        target: page.operations
        function onImageRemoved(id) {
            if (id === page.imageId || id === controller.primaryTag) {
                page.closeRequested();
            }
        }
    }
}
