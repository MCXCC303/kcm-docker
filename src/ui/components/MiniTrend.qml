/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Minimal trend bar: recent samples as a row of thin bars (ARCH_V2 §22 sparkline stand-in).

    Deliberately lightweight: no Canvas, no JS maths in the delegate —
    each sample just maps to one rectangle against the given maximum.
*/

import QtQuick
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

import "." as Local

RowLayout {
    id: trend

    /*! Sample series (number array, from MetricsModel's *History properties). */
    property var values: []
    /*! Normalisation ceiling; <= 0 derives the series maximum. */
    property real maxValue: 0
    /*!
        Bar colour defaults to the ChartPalette CPU series colour. Series colours must come from
        ChartPalette, never from status semantics such as positive/negative (§1.4) — otherwise
        green would mean both "running" and "network traffic".
    */
    property color barColor: Local.ChartPalette.cpuSeries
    /*!
        How many slots to always render (default: MetricsModel's ring-buffer capacity).
        Fixed slot count = constant Repeater model = samples never destroy/create bars.
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

    /*  Fixed slot count (classic sparkline trick):
        values (cpuHistory etc.) is a QVariantList rebuilt on every 5 s sample, so any
        model that follows the data — the array itself or merely its length — makes the
        Repeater destroy/create bars, and those bars sit in Kirigami.FormLayout
        (GridLayout) entries: a real session segfaulted exactly on "entry destroyed
        while the layout computes sizes" (ARCH_V3 appendix A.1d/A.1g). Constant slots
        remove that path entirely; slots without data stay empty, newest sample right. */
    Repeater {
        model: trend.maxSamples

        delegate: Rectangle {
            required property int index

            objectName: "trendBar"
            /*! Sample index for this slot; negative (empty slot) when data is short. */
            readonly property int sampleIndex: trend.values.length - trend.maxSamples + index
            readonly property bool hasSample: sampleIndex >= 0 && sampleIndex < trend.values.length
            readonly property real sampleValue: hasSample ? trend.values[sampleIndex] : 0

            Layout.fillHeight: true
            Layout.preferredWidth: trend.barWidth
            radius: width / 2
            // No alpha fade: ChartPalette colours are already contrast-checked (§1.8),
            // and multiplying in an alpha would drop effective contrast back out of range.
            color: trend.barColor
            // Floor at 1px so visible-but-tiny values still show; empty slots draw nothing
            Layout.preferredHeight: hasSample ? Math.max(1, parent.height * Math.min(1, sampleValue / trend.effectiveMax)) : 0
            Layout.alignment: Qt.AlignBottom
        }
    }
}
