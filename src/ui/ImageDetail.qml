/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Image Detail（ARCH_V2 §8/§52）：Repository / Tag / Digest / Size / Architecture / OS /
    Layers / 使用该镜像的容器（只读关联）。
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

    property string imageId: ""

    readonly property var controller: kcm.controller.imageDetail
    readonly property bool ready: controller.loadStateKey === "ready"


    /*! 请求返回列表页（由 main.qml 接 StackView.pop）。
        注意：不能叫 backRequested——Kirigami.Page 已经声明了同名信号。 */
    signal closeRequested

    Component.onCompleted: {
        // 同上：二次进入同一镜像也要重新加载
        if (page.imageId.length > 0) {
            controller.imageId = page.imageId;
            controller.start();
        }
    }

    Component.onDestruction: controller.stop()


    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

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

        // ---------------------------------------------------------------- //
        // Overview                                                          //
        // ---------------------------------------------------------------- //
        Kirigami.FormLayout {
            Layout.fillWidth: true
            visible: page.ready

            RowLayout {
                Kirigami.FormData.label: i18n("Repository:")

                QQC2.Label {
                    text: controller.primaryRepository.length > 0 ? controller.primaryRepository : i18n("<none> (dangling)")
                }
                QQC2.ToolButton {
                    icon.name: "edit-copy"
                    display: QQC2.AbstractButton.IconOnly
                    enabled: controller.primaryRepository.length > 0
                    QQC2.ToolTip.text: i18n("Copy repository")
                    QQC2.ToolTip.visible: hovered
                    onClicked: Kontainer.Presentation.copyToClipboard(controller.primaryRepository)
                }
            }
            RowLayout {
                Kirigami.FormData.label: i18n("Tag:")

                QQC2.Label {
                    text: controller.tagName.length > 0 ? controller.tagName : i18n("<none>")
                }
                QQC2.ToolButton {
                    icon.name: "edit-copy"
                    display: QQC2.AbstractButton.IconOnly
                    enabled: controller.tagName.length > 0
                    QQC2.ToolTip.text: i18n("Copy tag")
                    QQC2.ToolTip.visible: hovered
                    onClicked: Kontainer.Presentation.copyToClipboard(controller.tagName)
                }
            }
            RowLayout {
                Kirigami.FormData.label: i18n("Full reference:")

                QQC2.Label {
                    text: controller.primaryTag.length > 0 ? controller.primaryTag : i18n("<none>")
                }
                QQC2.ToolButton {
                    icon.name: "edit-copy"
                    display: QQC2.AbstractButton.IconOnly
                    enabled: controller.primaryTag.length > 0
                    QQC2.ToolTip.text: i18n("Copy image reference")
                    QQC2.ToolTip.visible: hovered
                    onClicked: Kontainer.Presentation.copyToClipboard(controller.primaryTag)
                }
            }
            RowLayout {
                Kirigami.FormData.label: i18n("Image ID:")

                QQC2.Label {
                    text: controller.shortId
                    font.family: "monospace"
                }
                QQC2.ToolButton {
                    icon.name: "edit-copy"
                    display: QQC2.AbstractButton.IconOnly
                    QQC2.ToolTip.text: i18n("Copy image ID")
                    QQC2.ToolTip.visible: hovered
                    onClicked: Kontainer.Presentation.copyToClipboard(controller.imageId)
                }
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

        // ---------------------------------------------------------------- //
        // Tags / Digests                                                    //
        // ---------------------------------------------------------------- //
        Kirigami.Heading {
            level: 3
            visible: page.ready && controller.tags.count > 0
            text: i18n("Tags")
        }
        Kirigami.Separator {
            Layout.fillWidth: true
            visible: page.ready && controller.tags.count > 0
        }

        Repeater {
            model: controller.tags

            delegate: QQC2.Label {
                required property string label

                Layout.fillWidth: true
                text: label
                font.family: "monospace"
                elide: Text.ElideMiddle
            }
        }

        Kirigami.Heading {
            level: 3
            visible: page.ready && controller.digests.count > 0
            text: i18n("Digests")
        }
        Kirigami.Separator {
            Layout.fillWidth: true
            visible: page.ready && controller.digests.count > 0
        }

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

        // ---------------------------------------------------------------- //
        // Layers                                                            //
        // ---------------------------------------------------------------- //
        Kirigami.Heading {
            level: 3
            visible: page.ready
            text: i18ncp("@info image layer count", "Layers (%1)", "Layers (%1)", controller.layerCount)
        }
        Kirigami.Separator {
            Layout.fillWidth: true
            visible: page.ready
        }

        QQC2.Label {
            Layout.fillWidth: true
            visible: page.ready
            text: i18n("The Docker API reports layer digests, not per-layer sizes.")
            font: Kirigami.Theme.smallFont
            opacity: 0.6
            wrapMode: Text.WordWrap
        }

        Repeater {
            model: controller.layers

            delegate: RowLayout {
                required property string label
                required property string value

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

        // ---------------------------------------------------------------- //
        // Containers using this image（只读关联，§52）                        //
        // ---------------------------------------------------------------- //
        Kirigami.Heading {
            level: 3
            visible: page.ready
            text: i18n("Containers")
        }
        Kirigami.Separator {
            Layout.fillWidth: true
            visible: page.ready
        }

        QQC2.Label {
            Layout.fillWidth: true
            visible: page.ready && controller.usedByContainers.empty
            text: i18n("No containers use this image.")
            opacity: 0.7
        }

        Repeater {
            model: controller.usedByContainers

            delegate: RowLayout {
                required property string label
                required property string value
                required property string detail
                required property string entryKey

                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                Kirigami.Icon {
                    source: Kontainer.Presentation.stateIconName(entryKey)
                    implicitWidth: Kirigami.Units.iconSizes.small
                    implicitHeight: Kirigami.Units.iconSizes.small
                }
                QQC2.Label {
                    text: label
                    Layout.fillWidth: true
                }
                QQC2.Label {
                    text: value
                    font: Kirigami.Theme.smallFont
                    opacity: 0.8
                }
            }
        }

        // ---------------------------------------------------------------- //
        // Configuration（默认折叠，§40）                                       //
        // ---------------------------------------------------------------- //
        ColumnLayout {
            Layout.fillWidth: true
            visible: page.ready
            spacing: 0

            QQC2.ItemDelegate {
                Layout.fillWidth: true
                text: i18ncp("@info environment variable count", "Environment (%1 variable)", "Environment (%1 variables)", controller.environmentCount)
                onClicked: imageEnvironmentValues.expanded = !imageEnvironmentValues.expanded

                contentItem: RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Icon {
                        source: imageEnvironmentValues.expanded ? "arrow-down" : "arrow-right"
                        implicitWidth: Kirigami.Units.iconSizes.small
                        implicitHeight: Kirigami.Units.iconSizes.small
                    }
                    QQC2.Label {
                        text: parent.parent.text
                        Layout.fillWidth: true
                    }
                    QQC2.Label {
                        visible: !imageEnvironmentValues.expanded
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
                id: imageEnvironmentValues

                objectName: "imageEnvironmentValues"
                property bool expanded: false
                visible: expanded
                Layout.fillWidth: true
                spacing: 0

                Repeater {
                    model: controller.environment

                    delegate: QQC2.Label {
                        required property string modelData

                        Layout.fillWidth: true
                        text: modelData
                        font.family: "monospace"
                        elide: Text.ElideMiddle
                    }
                }
            }
        }
    }
}
