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

    /*! 点"跳转"：请求打开这个容器的详情。 */
    signal containerRequested(string containerId, string containerName)
    /*! 点"停止"：请求停止该容器（由页面负责确认与执行）。 */
    signal stopRequested(string containerId, string containerName)

    objectName: "hostPortListView"

    QQC2.ScrollView {
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
                        Layout.preferredWidth: Kirigami.Units.gridUnit * 7
                        text: row.portText
                        font.family: "monospace"
                        font.bold: true
                    }

                    QQC2.Label {
                        objectName: "hostPortRowAddress"
                        Layout.preferredWidth: Kirigami.Units.gridUnit * 11
                        text: row.addressText === row.portText ? "—" : row.addressText
                        opacity: 0.75
                        elide: Text.ElideRight
                    }

                    QQC2.Label {
                        objectName: "hostPortRowMapping"
                        Layout.preferredWidth: Kirigami.Units.gridUnit * 10
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
                        semanticKey: row.stateKey === "inUse" ? "positive" : "neutral"
                        iconName: row.stateKey === "inUse" ? "media-playback-start" : "dialog-information"
                        text: row.stateKey === "inUse" ? i18n("In use") : i18n("Declared")
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
                        Layout.preferredWidth: Kirigami.Units.gridUnit * 12
                        Layout.maximumWidth: Kirigami.Units.gridUnit * 12
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
