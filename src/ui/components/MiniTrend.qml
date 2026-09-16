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
    property int barWidth: 3

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

    Repeater {
        model: trend.values

        delegate: Rectangle {
            required property var modelData

            Layout.fillHeight: true
            Layout.preferredWidth: trend.barWidth
            radius: width / 2
            // 不做透明度衰减：ChartPalette 的取色已按对比度校验过（§1.8），
            // 再乘一个 alpha 会把有效对比度拉回不达标区间。
            color: trend.barColor
            // 最低 1px，保证“有值但很小”也能看见
            Layout.preferredHeight: Math.max(1, parent.height * Math.min(1, modelData / trend.effectiveMax))
            Layout.alignment: Qt.AlignBottom
        }
    }
}
