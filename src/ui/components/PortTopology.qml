/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    端口映射拓扑（ARCH_V4 §2.1.2）。

    形态：左列容器端口、右列宿主绑定，中间是连线，两列之上各有一个节点标题
    （容器名 / 宿主主机名）。连线只是**装饰**——

      - 全部信息都写在芯片的文字里（`80/tcp` / `0.0.0.0:8080`）
      - 连线层 `Accessible.ignored`，键盘与屏幕阅读器完全不需要它
      - 未发布的端口没有宿主端点，因此不画线，由页面单独成组呈现

    为什么几何全部由 index 推导（而不是读取芯片的实际位置）：
    读测量值会引入「先测量 → 再布局 → 再画线」的一帧延迟，
    刷新时连线会短暂错位；`headerHeight + index * rowHeight` 是确定性的。

    为什么用 Canvas 而不是 Shape：Shape 的子对象必须是 ShapePath，
    「一条映射一条线」只能靠 Repeater，而 Repeater 是 Item（未定义用法）。
    Canvas 是单个 Item、一次 onPaint 画 N 条线，没有 delegate、不参与布局
    （ARCH_V4 §2.1.2 与附录 A.1）。
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

import "." as Local

Item {
    id: topology

    /*! 已发布的端口映射模型（`PortMappingModel`）。 */
    required property var model
    /*! 容器节点标题（通常是容器名）。 */
    required property string containerLabel
    /*! 宿主节点标题（通常是 daemon 报告的宿主主机名）。 */
    required property string hostLabel

    objectName: "portTopology"

    /*! 行高：足够放下一枚芯片，且与主题字号相关。 */
    readonly property real rowHeight: Math.ceil(Kirigami.Units.gridUnit * 1.6)
    /*! 节点标题行的高度。 */
    readonly property real headerHeight: Math.ceil(Kirigami.Units.gridUnit * 2.2)
    /*! 中间连线区的左右缩进：芯片列宽度。 */
    readonly property real chipColumnWidth: Math.ceil(Kirigami.Units.gridUnit * 9)

    implicitHeight: headerHeight + model.count * rowHeight

    /* ---------- 连线层（先画，位于芯片之下） ---------- */
    Canvas {
        id: linkLayer

        objectName: "portTopologyLinks"

        anchors.fill: parent
        // 纯装饰：不承载任何信息，也不参与键盘导航
        Accessible.ignored: true

        readonly property color linkColor: Local.ChartPalette.topologyLink

        onLinkColorChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        Connections {
            target: topology.model
            function onCountChanged() {
                linkLayer.requestPaint();
            }
        }

        onPaint: {
            const ctx = getContext("2d");
            ctx.reset();
            ctx.strokeStyle = linkLayer.linkColor;
            ctx.lineWidth = Math.max(1, Math.round(Kirigami.Units.smallSpacing / 4) / 2);
            ctx.lineCap = "round";

            const left = topology.chipColumnWidth;
            const right = linkLayer.width - topology.chipColumnWidth;
            if (right <= left) {
                return;
            }

            for (let row = 0; row < topology.model.count; ++row) {
                const y = topology.headerHeight + row * topology.rowHeight + topology.rowHeight / 2;
                ctx.beginPath();
                ctx.moveTo(left, y);
                ctx.lineTo(right, y);
                ctx.stroke();
            }
        }
    }

    /* ---------- 两列的节点标题 ---------- */
    RowLayout {
        id: header

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: topology.headerHeight
        spacing: Kirigami.Units.smallSpacing

        Rectangle {
            Layout.preferredWidth: topology.chipColumnWidth
            Layout.fillHeight: true
            Layout.maximumHeight: Math.ceil(Kirigami.Units.gridUnit * 1.7)
            Layout.alignment: Qt.AlignVCenter
            radius: Kirigami.Units.smallSpacing
            color: Local.ChartPalette.topologyNodeBackground
            border.width: 1
            border.color: Local.ChartPalette.topologyNodeBorder

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Kirigami.Units.smallSpacing
                anchors.rightMargin: Kirigami.Units.smallSpacing
                spacing: Kirigami.Units.smallSpacing

                Kirigami.Icon {
                    source: "application-x-executable"
                    implicitWidth: Kirigami.Units.iconSizes.small
                    implicitHeight: Kirigami.Units.iconSizes.small
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    text: topology.containerLabel
                    elide: Text.ElideMiddle
                    font.bold: true
                }
            }
        }

        Item {
            Layout.fillWidth: true
        }

        Rectangle {
            Layout.preferredWidth: topology.chipColumnWidth
            Layout.fillHeight: true
            Layout.maximumHeight: Math.ceil(Kirigami.Units.gridUnit * 1.7)
            Layout.alignment: Qt.AlignVCenter
            radius: Kirigami.Units.smallSpacing
            color: Local.ChartPalette.topologyNodeBackground
            border.width: 1
            border.color: Local.ChartPalette.topologyNodeBorder

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Kirigami.Units.smallSpacing
                anchors.rightMargin: Kirigami.Units.smallSpacing
                spacing: Kirigami.Units.smallSpacing

                Kirigami.Icon {
                    source: "computer"
                    implicitWidth: Kirigami.Units.iconSizes.small
                    implicitHeight: Kirigami.Units.iconSizes.small
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    text: topology.hostLabel
                    elide: Text.ElideMiddle
                    font.bold: true
                }
            }
        }
    }

    /* ---------- 芯片行（一行一条映射，左右两侧各一枚） ---------- */
    Repeater {
        model: topology.model

        delegate: Item {
            required property int index
            required property string containerChipText
            required property string hostChipText

            objectName: "portMappingRow"
            width: topology.width
            height: topology.rowHeight
            y: topology.headerHeight + index * topology.rowHeight

            Local.FieldChip {
                objectName: "portContainerChip"
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                anchors.rightMargin: Kirigami.Units.smallSpacing
                // 端口文本是数据，等宽字体更易比对（§1.5）
                font.family: "monospace"
                text: parent.containerChipText
            }

            Local.FieldChip {
                objectName: "portHostChip"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                font.family: "monospace"
                text: parent.hostChipText
            }
        }
    }
}
