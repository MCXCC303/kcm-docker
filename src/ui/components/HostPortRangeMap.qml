/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    端口区间地图（ARCH_next_ports.md §4.B，里程碑 M4）。

    端口列表回答"哪个端口被谁占着"，地图回答另一个问题：**这一段里哪里还空着**——
    不用读数字，扫一眼颜色就知道。

    设计取舍（实测）：
      - 不画 0-65535：只画**用到的端口附近**那几段（聚类规则在 C++ 的
        `HostPortUsage::clusterRanges`，每段两侧会留几个空闲端口）；
      - 一段里最多画 N 个方块，超出部分显示"还有 N 个"——否则 1000-1100 这种
        区间会一次性创建上百个方块，把界面拖垮；
      - 颜色只是辅助：方块上写着端口号，图例也写全（无障碍要求）。
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

import "." as Local

Item {
    id: root

    /*! `{first, last, title, tileCount, hiddenCount, usedCount, tiles:[{port,text,stateKey,occupied}]}` 的列表。 */
    required property var ranges
    /*! 下一个空闲宿主端口（0 = 找不到）。 */
    required property int nextFreePort

    objectName: "hostPortRangeMap"

    ColumnLayout {
        anchors.fill: parent
        spacing: Kirigami.Units.smallSpacing

        /* 下一个空闲端口：只读页面不"跳"，给出来 + 一键复制（创建表单里才是"一键采用"） */
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing
            visible: root.nextFreePort > 0

            QQC2.Label {
                objectName: "portMapNextFreeLabel"
                text: i18n("Next free host port: %1", root.nextFreePort)
            }

            Local.CopyButton {
                objectName: "portMapCopyNextFree"
                value: String(root.nextFreePort)
                fieldLabel: i18n("next free host port")
            }

            Item {
                Layout.fillWidth: true
            }
        }

        /* 图例：颜色永远配文字 */
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.largeSpacing

            Repeater {
                // 与列表视图同一套措辞（用户要求统一）：运行中 / 未启动 / 被占用 / 未占用 / 空闲
                model: [
                    {text: i18n("Running"), stateKey: "inUse"},
                    {text: i18n("Not started"), stateKey: "reserved"},
                    {text: i18n("Taken"), stateKey: "reservedTaken"},
                    {text: i18n("Not bound"), stateKey: "declaredNotPublished"},
                    {text: i18n("Free"), stateKey: ""}
                ]

                delegate: RowLayout {
                    required property var modelData
                    spacing: Kirigami.Units.smallSpacing / 2

                    Rectangle {
                        Layout.preferredWidth: Kirigami.Units.gridUnit * 0.7
                        Layout.preferredHeight: Kirigami.Units.gridUnit * 0.7
                        radius: 2
                        color: Local.StatusPalette.portTileColor(modelData.stateKey)
                        // 空闲：细边框 + 极低对比的填充（"空"看起来就该是空的；
                        // QML 的 Rectangle 没有 border.style，虚线只能靠 Canvas，不值当）
                        border.width: modelData.stateKey.length === 0 ? 1 : 2
                        border.color: Local.StatusPalette.portTileBorderColor(modelData.stateKey)
                    }

                    QQC2.Label {
                        text: modelData.text
                        font: Kirigami.Theme.smallFont
                        opacity: 0.85
                    }
                }
            }
        }

        QQC2.ScrollView {
            id: scroll

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            ColumnLayout {
                width: scroll.availableWidth
                spacing: Kirigami.Units.largeSpacing

                Repeater {
                    model: root.ranges

                    delegate: ColumnLayout {
                        required property var modelData

                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing / 2

                        RowLayout {
                            Layout.fillWidth: true

                            QQC2.Label {
                                objectName: "portMapRangeTitle"
                                text: modelData.title
                                font.family: "monospace"
                                font.bold: true
                            }

                            QQC2.Label {
                                objectName: "portMapRangeSummary"
                                // 这一段里非空闲的端口既有"正在使用"也有"被声明"，措辞不要写成"in use"
                                text: i18np("%1 port used by containers", "%1 ports used by containers", modelData.usedCount)
                                font: Kirigami.Theme.smallFont
                                opacity: 0.75
                            }

                            Item {
                                Layout.fillWidth: true
                            }

                            /* 超出上限的端口数：必须说出来，不能让用户以为这段就这么长 */
                            QQC2.Label {
                                objectName: "portMapHiddenCount"
                                visible: modelData.hiddenCount > 0
                                text: i18n("%1 more not shown", modelData.hiddenCount)
                                font: Kirigami.Theme.smallFont
                                opacity: 0.75
                            }
                        }

                        Flow {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing / 2

                            Repeater {
                                model: modelData.tiles

                                delegate: Rectangle {
                                    required property var modelData

                                    objectName: "portMapTile"
                                    width: Kirigami.Units.gridUnit * 3.2
                                    height: Kirigami.Units.gridUnit * 2.2
                                    radius: 4
                                    color: Local.StatusPalette.portTileColor(modelData.stateKey)
                                    border.width: modelData.occupied ? 1 : 1
                                    border.color: Local.StatusPalette.portTileBorderColor(modelData.stateKey)

                                    QQC2.Label {
                                        objectName: "portMapTileLabel"
                                        anchors.centerIn: parent
                                        text: modelData.text
                                        // 端口号用等宽字体（对齐好看），字号跟着主题的小字号
                                        font.family: "monospace"
                                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                                        color: Local.StatusPalette.portTileTextColor(modelData.stateKey)
                                    }

                                    Accessible.role: Accessible.StaticText
                                    Accessible.name: modelData.occupied
                                        ? i18n("Port %1: %2", modelData.port, modelData.stateKey)
                                        : i18n("Port %1: free", modelData.port)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
