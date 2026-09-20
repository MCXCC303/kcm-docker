/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Storage usage visualization (ARCH_V3 §2.4 / ARCH_V3_pre §1.9):

    Horizontal stacked bar + legend. The four segments are images / containers / volumes /
    build cache, using four brightness levels of one hue (neutral data display, not status
    semantics, so this uses a dedicated ChartPalette scale rather than semantic colors such
    as positive/negative).

    Design points:

    - **No faking**: categories the API does not provide (negative value) are neither drawn
      in the bar nor shown as 0; the legend shows "—" (following ARCH_V2 §23: unavailable ≠ 0).
    - **Not distinguished by color alone** (§1.8): segments are separated by a
      background-colored line, the legend provides "swatch + text + value" triple encoding,
      and the whole bar has an accessible name.
    - **No new accounting**: this component only visualizes; all values come from
      StorageStatus. Even when unavailable categories make the segment sum smaller than
      Total, it is shown as-is, without normalization.
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.kcm.docker as Kontainer

import "." as Local

ColumnLayout {
    id: bar

    /*! Data source: Kontainer.StorageStatus (`controller.storage`). */
    required property var storage

    spacing: Kirigami.Units.smallSpacing

    /*!
        All categories (for the legend). The whole build-cache row disappears when it does not
        exist — unlike "unavailable shows —": the API has no such field, versus a field whose
        value cannot be read.
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

    /*! Categories that can be drawn in the bar: value must be valid (≥ 0). */
    readonly property var drawableSegments: bar.segments.filter(segment => segment.value >= 0)

    /*! Total of the drawable categories, used as the width basis for each segment. */
    readonly property real drawableTotal: bar.drawableSegments.reduce((sum, segment) => sum + segment.value, 0)

    /*! Whether there is any drawable data. */
    readonly property bool hasDrawableData: bar.drawableTotal > 0

    function sizeText(bytes) {
        const text = Kontainer.Format.byteSize(bytes);
        return text.length > 0 ? text : i18n("—");
    }

    /*! Accessible name: reads all segments at once, not relying on color (§1.8). */
    function accessibleSummary(): string {
        const parts = [];
        for (const segment of bar.segments) {
            parts.push(i18nc("@info storage category and size, for screen readers", "%1: %2", segment.label, bar.sizeText(segment.value)));
        }
        return i18nc("@info screen reader summary of Docker disk usage", "Docker storage usage. %1", parts.join(", "));
    }

    /* ------------------------------------------------------------------ */
    /* Stacked bar                                                         */
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

        // The bar is empty while data is unavailable: explain it in words, not with an unreadable empty slot
        visible: bar.hasDrawableData

        Accessible.role: Accessible.Indicator
        Accessible.name: bar.accessibleSummary()

        Row {
            anchors.fill: parent

            /* model takes a length rather than a JS array: an array is re-evaluated on every
               refresh, and using it directly as model destroys and recreates the entries
               (see the same note in MainPage). */
            Repeater {
                model: bar.drawableSegments.length

                delegate: Rectangle {
                    required property int index

                    readonly property var segment: bar.drawableSegments[index]

                    width: bar.drawableTotal > 0 ? track.width * (segment.value / bar.drawableTotal) : 0
                    height: track.height
                    color: segment.color

                    // Adjacent segments differ by only ~1.2:1 in brightness, so a separator is needed (§1.8)
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
    /* Legend: swatch + name + value (triple encoding, §1.8)              */
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
