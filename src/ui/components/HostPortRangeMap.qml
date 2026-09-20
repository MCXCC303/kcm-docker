/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Port range map (ARCH_next_ports.md §4.B, milestone M4).

    The port list answers "who holds which port"; the map answers **where a range is still
    free** — no numbers to read, one glance at the colours.

    Design trade-offs (measured):
      - no 0-65535: only the few ranges **around used ports** (clustering in C++
        `HostPortUsage::clusterRanges`, a few free ports kept on each side);
      - at most N tiles per range, the rest shown as "N more" — otherwise a range like
        1000-1100 would build hundreds of tiles at once and stall the UI;
      - colour is an aid only: every tile carries its port number and the legend is
        complete (accessibility requirement).
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

import "." as Local

Item {
    id: root

    /*! List of `{first, last, title, tileCount, hiddenCount, usedCount, tiles:[...]}`. */
    required property var ranges
    /*! Next free host port (0 = none found). */
    required property int nextFreePort

    /*! Click a used port: request the container behind it (a running port maps to one container). */
    signal containerRequested(string containerId, string containerName)

    objectName: "hostPortRangeMap"

    ColumnLayout {
        anchors.fill: parent
        spacing: Kirigami.Units.smallSpacing

        /* Next free port: read-only pages do not "jump", they show it + copy it (the form adopts it) */
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

        /* Legend: colour always comes with text */
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.largeSpacing

            Repeater {
                // Same wording as the list view: Running / Not started / Taken / Not bound / Free
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
                        // Free: thin border + very low-contrast fill ("empty" should look empty;
                        // QML Rectangle has no border.style, dashes would need Canvas — not worth it)
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
                                // Non-free ports here are both used and declared, so do not word it "in use"
                                text: i18np("%1 port used by containers", "%1 ports used by containers", modelData.usedCount)
                                font: Kirigami.Theme.smallFont
                                opacity: 0.75
                            }

                            Item {
                                Layout.fillWidth: true
                            }

                            /* Ports beyond the cap: state them, or users think the range is that short */
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
                                    id: tile

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
                                        // Monospace ports (nice alignment), size from the theme small font
                                        font.family: "monospace"
                                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                                        color: Local.StatusPalette.portTileTextColor(modelData.stateKey)
                                    }

                                    /*
                                     * Click to navigate: only "running" ports do (unique container).
                                     *
                                     * **No tooltip here**: `QQC2.ToolTip.text/visible` are attached
                                     * properties sharing one tooltip per window, while the map holds
                                     * dozens of tiles whose delegates are destroyed and rebuilt on
                                     * filter/view changes — measured leftovers showed the same
                                     * container name wherever the mouse went and survived switching
                                     * back to the list. The container name comes from
                                     * `Accessible.name` instead (visible to screen readers, no leftovers).
                                     */
                                    MouseArea {
                                        objectName: "portMapTileClick"
                                        anchors.fill: parent
                                        enabled: tile.modelData.containerId !== undefined
                                            && String(tile.modelData.containerId).length > 0
                                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                        onClicked: root.containerRequested(tile.modelData.containerId, tile.modelData.containerName)
                                    }

                                    Accessible.role: Accessible.StaticText
                                    Accessible.name: {
                                        if (!modelData.occupied) {
                                            return i18n("Port %1: free", modelData.port);
                                        }
                                        if (tile.modelData.containerId !== undefined
                                                && String(tile.modelData.containerId).length > 0) {
                                            return i18n("Port %1: used by %2", modelData.port, tile.modelData.containerName);
                                        }
                                        return i18n("Port %1: %2", modelData.port, modelData.stateKey);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
