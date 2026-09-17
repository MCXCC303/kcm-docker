/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    端口映射拓扑（ARCH_V4 §2.1.2）。

    形态：左列容器端口、右列宿主绑定，中间是**带端点圆点的彩色连线**；
    同一个容器端口映射到多个宿主地址时，左列只出现**一枚**芯片，连线从同一个
    起点**分支**出去（每条的终点在右侧对应芯片的垂直中心），而不是把同一个端口
    在左列重复很多遍（ARCH_V5_V8 §2.1 拓扑形态修订）。
    两列之上各有一个节点标题（容器名 / 宿主主机名）。连线只是**装饰**——

      - 全部信息都写在芯片的文字里（`80/tcp` / `0.0.0.0:8080`）
      - 连线层 `Accessible.ignored`，键盘与屏幕阅读器完全不需要它
      - 未发布的端口没有宿主端点，因此不画线，由页面单独成组呈现

    连线颜色由 `colorSeed`（容器 id）决定，同一个容器永远同色（见
    `ChartPalette.connectionColor`）：颜色不表达任何语义，只让"同一个容器的图"
    看起来是一体的；端口号与绑定地址由两侧芯片的文字承载，颜色不是唯一区分手段。

    分支的纵向位置同样由 index 推导：第 i 条绑定的终点在组内第 i 行的中心，
    起点在整组的垂直中心。因此组的高度是 `bindingCount * rowHeight`。

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

    /*!
     * 已发布端口的分组模型（`PortMappingGroupModel`）。
     *
     * 每行 = 一个容器端口 + 它的全部宿主绑定：`hostChipTexts` 是右侧每一枚芯片的文本。
     */
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

    /*! 容器端口那一行的高度（左侧芯片所在行）。 */
    readonly property real rowHeight: Math.ceil(Kirigami.Units.gridUnit * 1.6)
    /*!
     * 右侧**每条绑定**占的高度。
     *
     * 比容器端口行更高：右边是"一个端口可能挂好几条绑定"的密集区域，
     * 行高与芯片一样高时上下会挤在一起（用户反馈）。分支终点按这个值居中，
     * 因此加大它同时也让曲线更舒展。
     */
    readonly property real bindingRowHeight: Math.ceil(Kirigami.Units.gridUnit * 2.4)
    /*! 节点标题行的高度。 */
    readonly property real headerHeight: Math.ceil(Kirigami.Units.gridUnit * 2.2)
    /*! 中间连线区的左右缩进：芯片列宽度。 */
    readonly property real chipColumnWidth: Math.ceil(Kirigami.Units.gridUnit * 9)

    // 高度按"行数 × 行高"算出（不读测量值）：连线几何与行位置由同一个数推导，
    // 刷新时不会出现"线已经画好、行还没布局"的错位（ARCH_V4 §2.1.2）
    implicitHeight: headerHeight + model.bindingCount * bindingRowHeight

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

    /* ------------------------------------------------------------------ */
    /* 分组行：一行 = 一个容器端口 + 它的全部宿主绑定                       */
    /*                                                                     */
    /* 左列只有一枚芯片（垂直居中于整组），右侧每条绑定各占一行；           */
    /* 连线从左侧同一个起点分支到每个绑定的终点（§2.1 拓扑形态修订）。       */
    /* ------------------------------------------------------------------ */
    Column {
        id: rowsColumn

        anchors.top: parent.top
        anchors.topMargin: topology.headerHeight
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 0

        Repeater {
            model: topology.model

            delegate: Item {
                id: group

                required property int index
                required property string containerChipText
                /*! 右侧每一枚芯片的文本（一个容器端口可能有多条绑定）。 */
                required property var hostChipTexts

                objectName: "portMappingRow"
                width: topology.width
                height: Math.max(topology.rowHeight, group.hostChipTexts.length * topology.bindingRowHeight)

                Canvas {
                    id: link

                    objectName: "portMappingLink"

                    anchors.fill: parent
                    Accessible.ignored: true

                    /*! 画了几条分支（= 该容器端口的绑定数）；用例据此断言"合并"确实发生。 */
                    readonly property int branchCount: group.hostChipTexts.length
                    /*! 起点圆环的颜色（= 最下方分支的颜色）。 */
                    readonly property color originColor: link.branchColor(group.hostChipTexts[group.hostChipTexts.length - 1])
                    /*! 每条分支的颜色（顺序与右侧芯片一致）：同一个容器 + 同一条映射永远同色。 */
                    readonly property var branchColors: {
                        const colors = [];
                        for (let i = 0; i < group.hostChipTexts.length; ++i) {
                            colors.push(link.branchColor(group.hostChipTexts[i]).toString());
                        }
                        return colors;
                    }

                    /*!
                     * 每条分支的颜色。
                     *
                     * 种子 = 容器 id + 容器端口芯片文本 + 该绑定的芯片文本：
                     * 同一个容器、同一条映射永远同色（刷新、重开页面都不变），
                     * 同一容器内的多条绑定又能彼此区分。颜色不承载语义（纯装饰，
                     * `Accessible.ignored`），信息在两侧芯片的文字里。
                     */
                    function branchColor(hostChipText: string): color {
                        return Local.ChartPalette.connectionColor(
                            topology.colorSeed + "|" + group.containerChipText + "|" + hostChipText);
                    }

                    readonly property real lineWidth: Math.max(2, Math.round(Kirigami.Units.gridUnit * 0.28))
                    readonly property real dotRadius: lineWidth * 1.15

                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()
                    Component.onCompleted: requestPaint()

                    onPaint: {
                        const ctx = getContext("2d");
                        ctx.reset();

                        // 起点：左侧芯片列之后；终点：右侧芯片列之前
                        const left = topology.chipColumnWidth;
                        const right = link.width - topology.chipColumnWidth;
                        const gap = Kirigami.Units.smallSpacing;
                        const originX = left + gap + link.dotRadius;
                        const targetX = right - gap - link.dotRadius;
                        if (targetX <= originX) {
                            return; // 宽度不够：不画（信息仍在芯片文字里）
                        }

                        const count = group.hostChipTexts.length;
                        const originY = link.height / 2;
                        // 分支的纵向间距 = 右侧绑定的行高：终点因此正好落在芯片中心
                        const step = topology.bindingRowHeight;

                        ctx.lineWidth = link.lineWidth;
                        ctx.lineCap = "round";

                        for (let i = 0; i < count; ++i) {
                            const color = link.branchColor(group.hostChipTexts[i]);
                            const targetY = step * (i + 0.5);

                            ctx.strokeStyle = color;
                            ctx.fillStyle = color;
                            ctx.beginPath();
                            ctx.moveTo(originX, originY);
                            // 三次贝塞尔：控制点让线"先直后弯"，与示意图一致
                            const bend = Math.max(Kirigami.Units.gridUnit, (targetX - originX) * 0.45);
                            ctx.bezierCurveTo(originX + bend, originY, targetX - bend, targetY, targetX, targetY);
                            ctx.stroke();

                            // 终点画成"插座"：外圈连线色、中心掏空成背景色
                            ctx.beginPath();
                            ctx.arc(targetX, targetY, link.dotRadius, 0, Math.PI * 2);
                            ctx.fill();
                            ctx.fillStyle = Kirigami.Theme.backgroundColor;
                            ctx.beginPath();
                            ctx.arc(targetX, targetY, link.dotRadius * 0.42, 0, Math.PI * 2);
                            ctx.fill();
                        }

                        // 起点只画一次（多条分支共用）。颜色取**最下方那条**分支：
                        // 分支按顺序绘制，越靠下的越在上层，因此起点圆环与"穿过起点的
                        // 那一条线"同色看起来才连贯（用户反馈：用最上面的颜色不协调）
                        const originColor = link.branchColor(group.hostChipTexts[count - 1]);
                        ctx.fillStyle = originColor;
                        ctx.beginPath();
                        ctx.arc(originX, originY, link.dotRadius, 0, Math.PI * 2);
                        ctx.fill();
                        ctx.fillStyle = Kirigami.Theme.backgroundColor;
                        ctx.beginPath();
                        ctx.arc(originX, originY, link.dotRadius * 0.42, 0, Math.PI * 2);
                        ctx.fill();
                    }
                }

                Local.FieldChip {
                    objectName: "portContainerChip"
                    // 靠**右**（靠近中间的节点列）：两侧芯片都朝节点收拢，图才紧凑
                    anchors.right: parent.right
                    anchors.rightMargin: Math.max(0, parent.width - topology.chipColumnWidth)
                    anchors.verticalCenter: parent.verticalCenter
                    // 端口文本是数据，等宽字体更易比对（§1.5）
                    font.family: "monospace"
                    text: group.containerChipText
                }

                Column {
                    // 靠**左**（紧接节点之后）：与容器侧一起把两列拉向中间
                    anchors.left: parent.left
                    anchors.leftMargin: Math.min(topology.width, topology.width - topology.chipColumnWidth + Kirigami.Units.smallSpacing)
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 0

                    Repeater {
                        model: group.hostChipTexts

                        delegate: Item {
                            id: bindingRow

                            required property string modelData

                            width: hostChip.width
                            // 与分支终点的间距一致：芯片中心 = bindingRowHeight * (i + 0.5)
                            height: topology.bindingRowHeight

                            Local.FieldChip {
                                id: hostChip

                                objectName: "portHostChip"
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                font.family: "monospace"
                                text: bindingRow.modelData
                            }
                        }
                    }
                }
            }
        }
    }
}
