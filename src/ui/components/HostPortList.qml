/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    宿主端口列表（ARCH_next_ports.md §4.A，里程碑 M3）。

    一行 = 一个宿主端口 + 谁在用它 + 它映射到容器里的哪个端口。
    状态只有两种（用户拍板：不做 reserved）：
      - `inUse`：运行中的容器**真的**发布了它；
      - `declaredNotPublished`：运行中的容器**声明**了它，但没真正发布——
        用于解释"为什么显示占用了却连不上"。

    用 `ListView` 而不是 Repeater：端口可能有上百行（用户机器上就有几十条映射）。
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

import "." as Local

Item {
    id: root

    /*! 过滤后的模型（`HostPortFilterModel`）。 */
    required property var model
    /*! 是否允许写操作（socket 可写）；否则不显示"停止容器"。 */
    required property bool writeAllowed

    /*! 状态 key → 语义色 / 图标 / 文字（颜色不单独承担语义，所以三者永远一起给）。 */
    function semanticKeyFor(stateKey: string): string {
        switch (stateKey) {
        case "inUse":
            return "positive";
        case "declaredNotPublished":
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
            return "dialog-warning";
        default:
            return "dialog-information";
        }
    }
    function stateText(stateKey: string): string {
        switch (stateKey) {
        case "inUse":
            return i18n("In use");
        case "declaredNotPublished":
            return i18n("Declared, not published");
        default:
            return i18n("Declared (container not running)");
        }
    }

    /*! 点"跳转"：请求打开这个容器的详情。 */
    signal containerRequested(string containerId, string containerName)
    /*! 点"停止"：请求停止该容器（由页面负责确认与执行）。 */
    signal stopRequested(string containerId, string containerName)

    objectName: "hostPortListView"

    /* 列宽只在这里定义一次：表头与数据行共用，改一处两边一起变 */
    readonly property real portWidth: Kirigami.Units.gridUnit * 7
    readonly property real addressWidth: Kirigami.Units.gridUnit * 13
    readonly property real mappingWidth: Kirigami.Units.gridUnit * 9
    readonly property real imageWidth: Kirigami.Units.gridUnit * 12

    /*!
     * 表头：用户实测反馈"不知道第二个参数是什么、那一列还总是 —"。
     *
     * 除了列出列名，地址列现在把"所有接口"写出来（不再是一个破折号），
     * 因为 `0.0.0.0` 对普通用户没有意义。
     */
    RowLayout {
        id: header

        objectName: "hostPortHeader"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Kirigami.Units.gridUnit * 1.6
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
            Layout.preferredWidth: Kirigami.Units.gridUnit * 11
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

    QQC2.ScrollView {
        anchors.top: header.bottom
        anchors.topMargin: Kirigami.Units.smallSpacing
        anchors.fill: parent
        clip: true

        ListView {
            id: listView

            objectName: "hostPortList"
            model: root.model
            spacing: 0

            QQC2.ScrollBar.vertical: QQC2.ScrollBar {}

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
                required property bool actionable

                objectName: "hostPortRow"
                width: listView.width
                hoverEnabled: true

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
                     * 状态用既有的 StatusChip（图标 + 颜色 + 文字三重编码）：
                     * 颜色语义由 C++ 的 Presentation 给，QML 不自己判状态字符串。
                     */
                    Local.StatusChip {
                        objectName: "hostPortRowState"
                        semanticKey: root.semanticKeyFor(row.stateKey)
                        iconName: root.iconNameFor(row.stateKey)
                        text: root.stateText(row.stateKey)
                    }

                    QQC2.Label {
                        objectName: "hostPortRowContainer"
                        // 容器名与镜像是"这一列"的信息：给固定宽度并省略，
                        // 否则它们会把右侧的动作按钮挤出可视区域（实测踩到）
                        Layout.fillWidth: true
                        Layout.preferredWidth: Kirigami.Units.gridUnit * 14
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

                    QQC2.Button {
                        objectName: "hostPortRowOpen"
                        text: i18n("Open")
                        display: QQC2.AbstractButton.TextBesideIcon
                        icon.name: "go-next-symbolic"
                        flat: true
                        onClicked: root.containerRequested(row.containerId, row.containerName)
                    }

                    QQC2.Button {
                        objectName: "hostPortRowStop"
                        visible: root.writeAllowed && row.actionable
                        text: i18n("Stop")
                        display: QQC2.AbstractButton.TextBesideIcon
                        icon.name: "process-stop"
                        flat: true
                        onClicked: root.stopRequested(row.containerId, row.containerName)
                    }
                }
            }
        }
    }
}
