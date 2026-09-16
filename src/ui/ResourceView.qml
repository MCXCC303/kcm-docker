/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    容器资源视图（ARCH_V2 §22 / ARCH_V3 §2.4、§2.5）：

    CPU / 内存 / 网络 / 块 I/O。

    - 所有百分比与速率都由 C++ 的 MetricsModel 算好（§18：统计公式不属于 UI），
      QML 只做单位与文本格式化。
    - 趋势线使用 ChartPalette 的**数据序列色**，不再借用 positive/negative
      这类状态语义色（§1.4 把数据可视化列为语义色的例外）。
    - 数值统一右对齐（§1.5）：同一列数字上下对齐，避免「12.3% / 97.4 MiB / 38.2 GiB」
      这种参差观感。
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.kontainer as Kontainer

import "components" as Components

ColumnLayout {
    id: view

    required property var metrics

    spacing: Kirigami.Units.smallSpacing

    function percentText(value) {
        return value >= 0 ? i18nc("@info percentage", "%1%", value.toFixed(1)) : i18n("—");
    }

    function rateText(bytesPerSecond) {
        if (bytesPerSecond < 0) {
            return i18n("—");
        }
        return i18nc("@info data rate per second", "%1/s", Kontainer.Format.byteSize(Math.round(bytesPerSecond)));
    }

    RowLayout {
        Layout.fillWidth: true

        QQC2.Label {
            text: i18n("Resources")
            font.bold: true
        }
        Item {
            Layout.fillWidth: true
        }
        QQC2.Label {
            visible: !view.metrics.sampling
            text: i18n("Not sampling (container is not running)")
            font: Kirigami.Theme.smallFont
            opacity: 0.7
        }
        QQC2.Label {
            visible: view.metrics.sampling
            text: i18ncp("@info sample count", "%1 sample", "%1 samples", view.metrics.sampleCount)
            font: Kirigami.Theme.smallFont
            opacity: 0.7
        }
    }

    Kirigami.FormLayout {
        Layout.fillWidth: true

        ColumnLayout {
            Kirigami.FormData.label: i18n("CPU:")

            QQC2.Label {
                Layout.fillWidth: true
                text: view.percentText(view.metrics.cpuPercent)
                horizontalAlignment: Text.AlignRight
                font.bold: true
            }
            Components.MiniTrend {
                visible: view.metrics.sampleCount > 1
                values: view.metrics.cpuHistory
                maxValue: 100
                barColor: Components.ChartPalette.cpuSeries
                Layout.preferredWidth: Kirigami.Units.gridUnit * 12
            }
        }

        ColumnLayout {
            Kirigami.FormData.label: i18n("Memory:")

            QQC2.Label {
                Layout.fillWidth: true
                text: {
                    if (!view.metrics.hasData) {
                        return i18n("—");
                    }
                    const used = Kontainer.Format.byteSize(view.metrics.memoryUsedBytes);
                    // 没有真实 limit 时只显示用量，避免“97 MiB / 38 GiB”这种误导（§19）
                    if (view.metrics.memoryLimitEffective) {
                        return i18nc("@info memory usage and limit", "%1 / %2", used, Kontainer.Format.byteSize(view.metrics.memoryLimitBytes));
                    }
                    return used;
                }
                horizontalAlignment: Text.AlignRight
                font.bold: true
            }
            QQC2.Label {
                Layout.fillWidth: true
                // 没有有效 memory limit 时不显示虚假百分比（§19）
                visible: view.metrics.memoryLimitEffective && view.metrics.memoryPercent >= 0
                text: view.percentText(view.metrics.memoryPercent)
                horizontalAlignment: Text.AlignRight
                font: Kirigami.Theme.smallFont
                opacity: 0.8
            }
            Components.MiniTrend {
                visible: view.metrics.sampleCount > 1
                values: view.metrics.memoryHistory
                barColor: Components.ChartPalette.memorySeries
                Layout.preferredWidth: Kirigami.Units.gridUnit * 12
            }
        }

        ColumnLayout {
            Kirigami.FormData.label: i18n("Network:")

            QQC2.Label {
                Layout.fillWidth: true
                text: i18nc("@info network receive rate", "↓ %1", view.rateText(view.metrics.networkRxPerSecond))
                horizontalAlignment: Text.AlignRight
            }
            QQC2.Label {
                Layout.fillWidth: true
                text: i18nc("@info network transmit rate", "↑ %1", view.rateText(view.metrics.networkTxPerSecond))
                horizontalAlignment: Text.AlignRight
            }
            Components.MiniTrend {
                visible: view.metrics.sampleCount > 1
                values: view.metrics.networkHistory
                barColor: Components.ChartPalette.networkSeries
                Layout.preferredWidth: Kirigami.Units.gridUnit * 12
            }
        }

        ColumnLayout {
            Kirigami.FormData.label: i18n("Block I/O:")

            QQC2.Label {
                Layout.fillWidth: true
                text: i18nc("@info block read rate", "Read %1", view.rateText(view.metrics.blockReadPerSecond))
                horizontalAlignment: Text.AlignRight
            }
            QQC2.Label {
                Layout.fillWidth: true
                text: i18nc("@info block write rate", "Write %1", view.rateText(view.metrics.blockWritePerSecond))
                horizontalAlignment: Text.AlignRight
            }
        }
    }

    QQC2.Label {
        Layout.fillWidth: true
        visible: !view.metrics.sampling && view.metrics.hasData
        text: i18n("Sampling stopped.")
        font: Kirigami.Theme.smallFont
        opacity: 0.7
    }
}
