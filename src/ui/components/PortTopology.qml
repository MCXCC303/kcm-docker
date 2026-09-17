/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    端口映射拓扑（ARCH_V4 §2.1.2）。

    形态：左列容器端口、右列宿主绑定，中间是一条**带端点圆点的彩色连线**，
    两列之上各有一个节点标题（容器名 / 宿主主机名）。连线只是**装饰**——

      - 全部信息都写在芯片的文字里（`80/tcp` / `0.0.0.0:8080`）
      - 连线层 `Accessible.ignored`，键盘与屏幕阅读器完全不需要它
      - 未发布的端口没有宿主端点，因此不画线，由页面单独成组呈现

    连线颜色由 `colorSeed`（容器 id）决定，同一个容器永远同色（见
    `ChartPalette.connectionColor`）：颜色不表达任何语义，只让"同一个容器的图"
    看起来是一体的；端口号与绑定地址由两侧芯片的文字承载，颜色不是唯一区分手段。

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
    /*!
     * 连线颜色的种子（通常传容器 id）。
     *
     * 同一个种子永远得到同一种颜色，因此刷新页面、重新打开详情、切换主题时，
     * 同一个容器的拓扑不会变色。
     */
    property string colorSeed

    /*! 本容器拓扑的连线颜色（由 `colorSeed` 决定）。 */
    readonly property color connectionColor: Local.ChartPalette.connectionColor(topology.colorSeed)

    objectName: "portTopology"

    /*! 行高：足够放下一枚芯片，且与主题字号相关。 */
    readonly property real rowHeight: Math.ceil(Kirigami.Units.gridUnit * 1.6)
    /*! 节点标题行的高度。 */
    readonly property real headerHeight: Math.ceil(Kirigami.Units.gridUnit * 2.2)
    /*! 中间连线区的左右缩进：芯片列宽度。 */
    readonly property real chipColumnWidth: Math.ceil(Kirigami.Units.gridUnit * 9)

    implicitHeight: headerHeight + model.count * rowHeight

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
            id: row

            required property int index
            required property string containerChipText
            required property string hostChipText

            objectName: "portMappingRow"
            width: topology.width
            height: topology.rowHeight
            y: topology.headerHeight + index * topology.rowHeight

            Canvas {
                id: link

                objectName: "portMappingLink"

                anchors.fill: parent
                Accessible.ignored: true

                /*!
                 * 这一行连线的颜色。
                 *
                 * 种子 = 容器 id + 这条映射自己的字段（端口 / 协议 / 绑定地址），
                 * 因此同一个容器、同一条映射永远同色（刷新、重开页面都不变），
                 * 同一容器内的多条映射又能彼此区分。颜色本身不承载语义：
                 * 它是纯装饰（`Accessible.ignored`），信息在两侧芯片的文字里。
                 */
                readonly property color linkColor: Local.ChartPalette.connectionColor(
                    topology.colorSeed + "|" + row.containerChipText + "|" + row.hostChipText)

                readonly property real lineWidth: Math.max(2, Math.round(Kirigami.Units.gridUnit * 0.28))
                readonly property real dotRadius: lineWidth * 1.15

                onLinkColorChanged: requestPaint()
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()

                onPaint: {
                    const ctx = getContext("2d");
                    ctx.reset();

                    const left = topology.chipColumnWidth;
                    const right = link.width - topology.chipColumnWidth;
                    const gap = Kirigami.Units.smallSpacing;
                    const from = left + gap + link.dotRadius;
                    const to = right - gap - link.dotRadius;
                    if (to <= from) {
                        return;
                    }

                    const y = link.height / 2;
                    ctx.strokeStyle = link.linkColor;
                    ctx.fillStyle = link.linkColor;
                    ctx.lineWidth = link.lineWidth;
                    ctx.lineCap = "round";

                    ctx.beginPath();
                    ctx.moveTo(from, y);
                    ctx.lineTo(to, y);
                    ctx.stroke();

                    // 端点画成"插座"：外圈连线色、中心掏空成背景色。
                    // Canvas 没有挖洞的合成操作，用背景色覆盖是唯一稳定做法
                    // （背景色来自主题，不硬编码 RGB）
                    for (const x of [from, to]) {
                        ctx.beginPath();
                        ctx.arc(x, y, link.dotRadius, 0, Math.PI * 2);
                        ctx.fill();
                    }
                    ctx.fillStyle = Kirigami.Theme.backgroundColor;
                    for (const x of [from, to]) {
                        ctx.beginPath();
                        ctx.arc(x, y, link.dotRadius * 0.42, 0, Math.PI * 2);
                        ctx.fill();
                    }
                }
            }

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
