/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    存储占用可视化（ARCH_V3 §2.4 / ARCH_V3_pre §1.9）：

    横向堆叠条 + 图例。四段分别是 镜像 / 容器 / 数据卷 / 构建缓存，
    使用同一色相的四级明度（属于中性数据展示，不是状态语义，因此
    使用 ChartPalette 的专用色阶而不是 positive/negative 这类语义色）。

    设计要点：

    - **不伪装**：API 未提供的类别（值为负）既不画进条里，也不显示为 0，
      图例里显示 “—”（沿用 ARCH_V2 §23 的约定：不可用 ≠ 0）。
    - **不靠颜色区分**（§1.8）：段与段之间有背景色分隔线，图例提供
      「色块 + 文字 + 数值」三重编码，整条还提供可访问名称。
    - **不新增统计口径**：本组件只做可视化，数值全部来自 StorageStatus；
      即使某些类别不可用导致各段之和小于 Total，也如实呈现，不做归一化。
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.kcm.docker as Kontainer

import "." as Local

ColumnLayout {
    id: bar

    /*! 数据源：Kontainer.StorageStatus（`controller.storage`）。 */
    required property var storage

    spacing: Kirigami.Units.smallSpacing

    /*!
        全部类别（图例用）。构建缓存不存在时整行不出现——
        这与「不可用显示 —」不同：前者是 API 没有这个字段，后者是有字段但取不到值。
    */
    readonly property var segments: {
        const list = [
            {
                entryKey: "images",
                label: i18n("Images"),
                value: bar.storage.imagesBytes,
                color: Local.ChartPalette.storageImages
            },
            {
                entryKey: "containers",
                label: i18n("Containers"),
                value: bar.storage.containersBytes,
                color: Local.ChartPalette.storageContainers
            },
            {
                entryKey: "volumes",
                label: i18n("Volumes"),
                value: bar.storage.volumesBytes,
                color: Local.ChartPalette.storageVolumes
            }
        ];
        if (bar.storage.buildCacheAvailable) {
            list.push({
                entryKey: "buildCache",
                label: i18n("Build cache"),
                value: bar.storage.buildCacheBytes,
                color: Local.ChartPalette.storageBuildCache
            });
        }
        return list;
    }

    /*! 可以画进条里的类别：值必须有效（≥ 0）。 */
    readonly property var drawableSegments: bar.segments.filter(segment => segment.value >= 0)

    /*! 可绘制类别的合计，用作各段宽度基准。 */
    readonly property real drawableTotal: bar.drawableSegments.reduce((sum, segment) => sum + segment.value, 0)

    /*! 是否有任何可绘制数据。 */
    readonly property bool hasDrawableData: bar.drawableTotal > 0

    function sizeText(bytes) {
        const text = Kontainer.Format.byteSize(bytes);
        return text.length > 0 ? text : i18n("—");
    }

    /*! 可访问名称：把各段一次性读出来，不依赖颜色（§1.8）。 */
    function accessibleSummary(): string {
        const parts = [];
        for (const segment of bar.segments) {
            parts.push(i18nc("@info storage category and size, for screen readers", "%1: %2", segment.label, bar.sizeText(segment.value)));
        }
        return i18nc("@info screen reader summary of Docker disk usage", "Docker storage usage. %1", parts.join(", "));
    }

    /* ------------------------------------------------------------------ */
    /* 堆叠条                                                              */
    /* ------------------------------------------------------------------ */
    Rectangle {
        id: track

        Layout.fillWidth: true
        implicitHeight: Kirigami.Units.gridUnit * 0.75
        radius: height / 2
        color: Kirigami.Theme.alternateBackgroundColor
        border.width: 1
        border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15)
        clip: true

        // 数据不可用时条是空的：此时给出文字说明，而不是一个看不懂的空槽
        visible: bar.hasDrawableData

        Accessible.role: Accessible.Indicator
        Accessible.name: bar.accessibleSummary()

        Row {
            anchors.fill: parent

            /* model 用长度而不是 JS 数组：数组每次刷新都会重新求值，
               直接当 model 会导致条目被销毁重建（见 MainPage 里同样的说明）。 */
            Repeater {
                model: bar.drawableSegments.length

                delegate: Rectangle {
                    required property int index

                    readonly property var segment: bar.drawableSegments[index]

                    width: bar.drawableTotal > 0 ? track.width * (segment.value / bar.drawableTotal) : 0
                    height: track.height
                    color: segment.color

                    // 相邻段的明度差只有约 1.2:1，必须有分隔线才能稳定区分（§1.8）
                    Rectangle {
                        visible: index > 0
                        width: 1
                        height: parent.height
                        color: Local.ChartPalette.storageSeparator
                    }
                }
            }
        }
    }

    QQC2.Label {
        Layout.fillWidth: true
        visible: !bar.hasDrawableData
        text: i18n("Storage usage is not available.")
        opacity: 0.7
        wrapMode: Text.WordWrap
        font: Kirigami.Theme.smallFont
    }

    /* ------------------------------------------------------------------ */
    /* 图例：色块 + 名称 + 数值（三重编码，§1.8）                            */
    /* ------------------------------------------------------------------ */
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 0

        Repeater {
            model: bar.segments.length

            delegate: RowLayout {
                required property int index

                objectName: "storageLegendEntry"
                readonly property var segment: bar.segments[index]

                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: Kirigami.Units.gridUnit * 0.6
                    implicitHeight: Kirigami.Units.gridUnit * 0.6
                    radius: width / 4
                    color: segment.value >= 0 ? segment.color : "transparent"
                    border.width: segment.value >= 0 ? 0 : 1
                    border.color: Local.StatusPalette.color("disabled")
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    text: segment.label
                    opacity: segment.value >= 0 ? 0.75 : 0.5
                }

                QQC2.Label {
                    text: bar.sizeText(segment.value)
                    horizontalAlignment: Text.AlignRight
                    opacity: segment.value >= 0 ? 1.0 : 0.5
                }
            }
        }
    }
}
