/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    极简趋势条：把短期采样画成一排细柱（ARCH_V2 §22 的 sparkline 替代品）。

    刻意保持轻量：不使用 Canvas、不在 delegate 里做 JS 计算，
    只按给定的最大值把每个采样映射成一根矩形。
*/

import QtQuick
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

import "." as Local

RowLayout {
    id: trend

    /*! 采样序列（数值数组，来自 MetricsModel 的 *History 属性）。 */
    property var values: []
    /*! 归一化上限；<= 0 时自动取序列最大值。 */
    property real maxValue: 0
    /*!
        柱色默认取 ChartPalette 的 CPU 序列色。
        注意：数据序列必须使用 ChartPalette 的专用色，不能用
        positive/negative 这类状态语义色（§1.4），否则「绿色」会同时表示
        「运行中」和「网络流量」。
    */
    property color barColor: Local.ChartPalette.cpuSeries
    /*!
        固定渲染多少个槽位（默认取 MetricsModel 的环形缓冲容量）。
        条数固定 = Repeater 的 model 永远不变 = 采样不会销毁/创建任何柱子。
    */
    property int maxSamples: 60
    property int barWidth: 2

    spacing: 1
    implicitHeight: Kirigami.Units.gridUnit * 1.5

    readonly property real effectiveMax: {
        if (maxValue > 0) {
            return maxValue;
        }
        let maximum = 0;
        for (let i = 0; i < values.length; ++i) {
            maximum = Math.max(maximum, values[i]);
        }
        return maximum > 0 ? maximum : 1;
    }

    /*  固定条数渲染（经典 sparkline 做法）：
        values（cpuHistory 等）是每 5 秒采样都会重新生成的 QVariantList，
        任何"跟着数据变化"的 model（无论是数组本身还是它的长度）都会让 Repeater
        销毁/创建柱子，而这些柱子正处在 Kirigami.FormLayout（GridLayout）的条目里——
        真实会话的段错误恰好发生在「布局算尺寸时条目被销毁」的路径上
        （ARCH_V3 附录 A.1d/A.1g）。条数固定后这条路径彻底消失，
        数据不足的槽位留空，最新采样始终贴右显示。 */
    Repeater {
        model: trend.maxSamples

        delegate: Rectangle {
            required property int index

            objectName: "trendBar"
            /*! 该槽位对应的采样下标；不足时为负（槽位为空）。 */
            readonly property int sampleIndex: trend.values.length - trend.maxSamples + index
            readonly property bool hasSample: sampleIndex >= 0 && sampleIndex < trend.values.length
            readonly property real sampleValue: hasSample ? trend.values[sampleIndex] : 0

            Layout.fillHeight: true
            Layout.preferredWidth: trend.barWidth
            radius: width / 2
            // 不做透明度衰减：ChartPalette 的取色已按对比度校验过（§1.8），
            // 再乘一个 alpha 会把有效对比度拉回不达标区间。
            color: trend.barColor
            // 最低 1px，保证“有值但很小”也能看见；空槽位不画
            Layout.preferredHeight: hasSample ? Math.max(1, parent.height * Math.min(1, sampleValue / trend.effectiveMax)) : 0
            Layout.alignment: Qt.AlignBottom
        }
    }
}
