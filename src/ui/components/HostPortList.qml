/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    宿主端口列表（ARCH_next_ports.md §4.A，里程碑 M3）。

    一行 = 一个宿主端口 + 谁在用它 + 它映射到容器里的哪个端口。
    三种状态（用户实测反馈后确定的措辞，短到一眼能读完）：
      - `inUse`                 运行中：运行中的容器**真的**发布了它；
      - `declaredNotPublished`  未占用：容器在跑，但声明的那条映射没真正生效
                                        （`--net=host` 之类会让 `-p` 被忽略）；
      - `reserved`              未启动：容器没在跑，端口现在是空的，
                                        但它一起来就会要回去。

    布局注意（用户实测反馈）：整页只保留**一个**滚动条——ScrollBar 交给 ScrollView 自带，
    不要再手写一个；表头也不再压在第一条上（用 ColumnLayout 分开排，并给表头底色）。

    交互（用户实测反馈）：条目**整行可点**即可跳转容器详情，"跳转/停止"按钮都去掉了
    （停止在容器详情页里有）。
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

import "." as Local

ColumnLayout {
    id: root

    /*! 过滤后的模型（`HostPortFilterModel`）。 */
    required property var model

    /*! 点整行：请求打开这个容器的详情。 */
    signal containerRequested(string containerId, string containerName)

    objectName: "hostPortListView"
    spacing: Kirigami.Units.smallSpacing

    /* 列宽只在这里定义一次：表头与数据行共用，改一处两边一起变 */
    readonly property real portWidth: Kirigami.Units.gridUnit * 7
    readonly property real addressWidth: Kirigami.Units.gridUnit * 13
    readonly property real mappingWidth: Kirigami.Units.gridUnit * 9
    readonly property real stateWidth: Kirigami.Units.gridUnit * 7
    readonly property real imageWidth: Kirigami.Units.gridUnit * 12

    /*! 状态 key → 语义色 / 图标 / 文字（颜色不单独承担语义，所以三者永远一起给）。 */
    function semanticKeyFor(stateKey: string): string {
        switch (stateKey) {
        case "inUse":
            return "positive";
        case "declaredNotPublished":
        case "reservedTaken":
            return "negative";
        default:
            return "neutral";
        }
    }
    function iconNameFor(stateKey: string): string {
        switch (stateKey) {
        case "inUse":
            return "media-playback-start";
        case "declaredNotPublished":
        case "reservedTaken":
            return "dialog-warning";
        default:
            return "dialog-information";
        }
    }
    /*! 状态文案：短（运行中 / 未占用 / 未启动），完整含义放悬停提示。 */
    function stateText(stateKey: string): string {
        switch (stateKey) {
        case "inUse":
            return i18n("Running");
        case "declaredNotPublished":
            return i18n("Not bound");
        case "reservedTaken":
            // 端口已经被别人占着：这个容器一起来就会端口冲突
            return i18n("Taken");
        default:
            return i18n("Not started");
        }
    }
    function stateHint(stateKey: string): string {
        switch (stateKey) {
        case "inUse":
            return i18n("A running container publishes this port.");
        case "declaredNotPublished":
            return i18n("The container is running, but this declared mapping is not actually bound on the host.");
        case "reservedTaken":
            return i18n("This port is currently used by another container, so this container will fail to start with a port conflict.");
        default:
            return i18n("This container needs to publish this port when it starts.");
        }
    }

    /*!
     * 表头（用户实测反馈：不知道各列是什么，第二列还总是 —）。
     *
     * 有底色（主题的交替背景色），并且与列表**分开排**——之前用 anchors 定位时
     * 第一条会被表头压住、看起来像图层串了。
     */
    Rectangle {
        id: headerBackground

        objectName: "hostPortHeaderBackground"
        Layout.fillWidth: true
        implicitHeight: header.implicitHeight + Kirigami.Units.smallSpacing * 2
        color: Kirigami.Theme.alternateBackgroundColor
        radius: 4

        RowLayout {
            id: header

            objectName: "hostPortHeader"
            anchors.fill: parent
            anchors.leftMargin: Kirigami.Units.smallSpacing
            anchors.rightMargin: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.smallSpacing

            QQC2.Label {
                objectName: "hostPortHeaderPort"
                Layout.preferredWidth: root.portWidth
                text: i18n("Host port")
                font.bold: true
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                opacity: 0.8
            }

            QQC2.Label {
                objectName: "hostPortHeaderAddress"
                Layout.preferredWidth: root.addressWidth
                text: i18n("Bind address")
                font.bold: true
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                opacity: 0.8
            }

            QQC2.Label {
                objectName: "hostPortHeaderMapping"
                Layout.preferredWidth: root.mappingWidth
                text: i18n("Container port")
                font.bold: true
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                opacity: 0.8
            }

            QQC2.Label {
                objectName: "hostPortHeaderState"
                Layout.preferredWidth: root.stateWidth
                text: i18n("State")
                font.bold: true
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                opacity: 0.8
            }

            QQC2.Label {
                objectName: "hostPortHeaderContainer"
                Layout.fillWidth: true
                text: i18n("Container")
                font.bold: true
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                opacity: 0.8
            }

            QQC2.Label {
                objectName: "hostPortHeaderImage"
                Layout.preferredWidth: root.imageWidth
                text: i18n("Image")
                font.bold: true
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                opacity: 0.8
            }
        }
    }

    QQC2.ScrollView {
        id: scroll

        // 只在这里滚动（ScrollBar 由 ScrollView 自带；再手写一个就会出现两条滚动条）
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true

        ListView {
            id: listView

            objectName: "hostPortList"
            model: root.model
            spacing: 0

            delegate: QQC2.ItemDelegate {
                id: row

                required property string portText
                required property string addressText
                required property bool wildcard
                required property int containerPort
                required property string protocol
                required property string stateKey
                required property string containerName
                required property string containerId
                required property string containerImage

                objectName: "hostPortRow"
                width: listView.width
                hoverEnabled: true
                // 整行可点 = 跳转（用户要求：不要额外的"跳转"按钮）
                onClicked: root.containerRequested(row.containerId, row.containerName)

                contentItem: RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    /* 端口是这一页的第一视觉焦点（用户要求：端口在前，容器只是其中一列） */
                    QQC2.Label {
                        objectName: "hostPortRowPort"
                        Layout.preferredWidth: root.portWidth
                        text: row.portText
                        font.family: "monospace"
                        font.bold: true
                    }

                    QQC2.Label {
                        objectName: "hostPortRowAddress"
                        Layout.preferredWidth: root.addressWidth
                        // 通配不再显示破折号：写清楚是"所有接口"（v4+v6 合并时也一样）
                        text: row.wildcard ? i18n("All interfaces") : row.addressText
                        opacity: 0.75
                        elide: Text.ElideRight
                    }

                    QQC2.Label {
                        objectName: "hostPortRowMapping"
                        Layout.preferredWidth: root.mappingWidth
                        // 端口/协议是技术写法（80/tcp），不做翻译，也不用 i18n 包
                        text: row.containerPort + "/" + row.protocol
                        font.family: "monospace"
                        opacity: 0.85
                    }

                    /*
                     * 状态列：给列一个固定宽度保证与表头对齐，但**徽标本身按内容收缩**——
                     * 直接把宽度给 StatusChip 会把它拉长，右侧留出一块空位（用户反馈）。
                     */
                    Item {
                        Layout.preferredWidth: root.stateWidth
                        Layout.fillHeight: true

                        Local.StatusChip {
                            objectName: "hostPortRowState"
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            semanticKey: root.semanticKeyFor(row.stateKey)
                            iconName: root.iconNameFor(row.stateKey)
                            text: root.stateText(row.stateKey)

                            // 短文案 + 悬停看完整含义（用户要求状态要短，但含义不能丢）
                            QQC2.ToolTip.text: root.stateHint(row.stateKey)
                            QQC2.ToolTip.visible: hovered
                        }
                    }

                    QQC2.Label {
                        objectName: "hostPortRowContainer"
                        Layout.fillWidth: true
                        Layout.minimumWidth: Kirigami.Units.gridUnit * 6
                        text: row.containerName
                        elide: Text.ElideRight
                    }

                    QQC2.Label {
                        objectName: "hostPortRowImage"
                        Layout.preferredWidth: root.imageWidth
                        Layout.maximumWidth: root.imageWidth
                        text: row.containerImage
                        opacity: 0.7
                        elide: Text.ElideMiddle
                    }

                    /* 整行可点的提示：只留一个图标，不再是按钮 */
                    Kirigami.Icon {
                        objectName: "hostPortRowChevron"
                        source: "go-next-symbolic"
                        implicitWidth: Kirigami.Units.iconSizes.small
                        implicitHeight: Kirigami.Units.iconSizes.small
                        opacity: 0.6
                    }
                }
            }
        }
    }
}
