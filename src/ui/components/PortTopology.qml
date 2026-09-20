/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Port mapping topology (ARCH_V4 §2.1.2).

    Shape: container ports in the left column, host bindings in the right, **colored links
    with end dots** in between. When one container port maps to several host addresses the
    left column shows **one** chip and the links **branch** from a single origin (each branch
    ending at the vertical center of its chip on the right), instead of repeating the same
    port many times on the left (ARCH_V5_V8 §2.1 topology revision).
    Each column has a node title above it (container name / host hostname). The links are
    **decoration** only:

      - all information is in the chip texts (`80/tcp` / `0.0.0.0:8080`)
      - the link layer is `Accessible.ignored`; keyboard and screen readers never need it
      - unpublished ports have no host endpoint, so no link is drawn; the page groups them apart

    Link color is derived from `colorSeed` (the container id), so one container always keeps
    its color (see `ChartPalette.connectionColor`): colors carry no semantics, they only make
    "the diagram of one container" look like one piece; port numbers and bind addresses are
    carried by the chip texts, so color is not the sole differentiator.

    Branch vertical positions are likewise derived from the index: the i-th binding ends at
    the center of row i in the group, and the origin sits at the group's vertical center.
    The group height is therefore `bindingCount * rowHeight`.

    Why all geometry is derived from indices (instead of reading actual chip positions):
    reading measurements adds a frame of "measure → lay out → draw" delay, so links briefly
    misalign on refresh; `headerHeight + index * rowHeight` is deterministic.

    Why Canvas instead of Shape: Shape children must be ShapePaths, so "one link per mapping"
    would need a Repeater, and a Repeater is an Item (undefined usage). Canvas is a single
    Item drawing N links in one onPaint, with no delegates and no layout participation
    (ARCH_V4 §2.1.2 and appendix A.1).
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

import "." as Local

Item {
    id: topology

    /*!
     * Grouped model of published ports (`PortMappingGroupModel`).
     *
     * One row = one container port plus all of its host bindings; `hostChipTexts` holds the
     * text of each chip on the right.
     */
    required property var model
    /*! Container node title (usually the container name). */
    required property string containerLabel
    /*! Host node title (usually the host hostname reported by the daemon). */
    required property string hostLabel
    /*!
     * Seed for link colors (usually the container id).
     *
     * One seed always yields one color, so a container's topology keeps its color across
     * refreshes, reopening the details, and theme changes.
     */
    property string colorSeed

    /*! Link color of this container's topology (derived from `colorSeed`). */
    readonly property color connectionColor: Local.ChartPalette.connectionColor(topology.colorSeed)

    objectName: "portTopology"

    /*! Height of the container-port row (the row holding the left chip). */
    readonly property real rowHeight: Math.ceil(Kirigami.Units.gridUnit * 1.6)
    /*!
     * Height of **each binding** on the right.
     *
     * Taller than the container-port row: the right side is the dense area where one port
     * may carry several bindings, and with chip-height rows they squeeze together (user
     * feedback). Branch endpoints center on this value, so enlarging it also spreads the
     * curves out.
     */
    readonly property real bindingRowHeight: Math.ceil(Kirigami.Units.gridUnit * 2.4)
    /*! Height of the node title row. */
    readonly property real headerHeight: Math.ceil(Kirigami.Units.gridUnit * 2.2)
    /*! Horizontal inset of the middle link area: the width of a chip column. */
    readonly property real chipColumnWidth: Math.ceil(Kirigami.Units.gridUnit * 9)

    // Height is computed from "rows × row height" (no measured values): link geometry and row
    // positions derive from the same number, so a refresh never shows links drawn before the
    // rows are laid out (ARCH_V4 §2.1.2)
    implicitHeight: headerHeight + model.bindingCount * bindingRowHeight

    /* ---------- Node titles of both columns ---------- */
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
    /* Group row: one row = one container port + all of its host bindings  */
    /*                                                                     */
    /* The left column has a single chip, **aligned with the group's first */
    /* host binding** (user feedback A5: as in network-example.svg, the    */
    /* first link is horizontal; centering would slant it across). Each    */
    /* binding on the right takes one row, and the links branch from that  */
    /* one left origin to each binding's endpoint.                         */
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
                /*! Text of each chip on the right (one container port may have several bindings). */
                required property var hostChipTexts
                /*! Same order as `hostChipTexts`: whether the binding is IPv4 + IPv6 (double ring). */
                required property var dualStackFlags

                objectName: "portMappingRow"
                width: topology.width
                height: Math.max(topology.rowHeight, group.hostChipTexts.length * topology.bindingRowHeight)

                Canvas {
                    id: link

                    objectName: "portMappingLink"

                    anchors.fill: parent
                    Accessible.ignored: true

                    /*! Branches drawn (= bindings of this container port); tests assert the merging. */
                    readonly property int branchCount: group.hostChipTexts.length
                    /*!
                     * Origin y (relative to this group): aligned with the center of the **first** binding.
                     *
                     * Exposed as a property so tests can assert "the first link is horizontal" (§A5)
                     * rather than only looking at chip positions — the two are decided by different
                     * code and must be guarded separately.
                     */
                    readonly property real originY: topology.bindingRowHeight * 0.5
                    /*! Color of the origin ring (= color of the lowest branch). */
                    readonly property color originColor: link.branchColor(group.hostChipTexts[group.hostChipTexts.length - 1])
                    /*! Color of each branch (chip order): same container + mapping, same color. */
                    readonly property var branchColors: {
                        const colors = [];
                        for (let i = 0; i < group.hostChipTexts.length; ++i) {
                            colors.push(link.branchColor(group.hostChipTexts[i]).toString());
                        }
                        return colors;
                    }

                    /*!
                     * Color of each branch.
                     *
                     * Seed = container id + container port chip text + that binding's chip text:
                     * one container and one mapping always keep their color (across refreshes and
                     * reopened pages), while several bindings in one container stay distinct.
                     * Colors carry no semantics (pure decoration, `Accessible.ignored`); the
                     * information is in the chip texts on both sides.
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

                        // Origin: after the left chip column; endpoints: before the right chip column
                        const left = topology.chipColumnWidth;
                        const right = link.width - topology.chipColumnWidth;
                        const gap = Kirigami.Units.smallSpacing;
                        const originX = left + gap + link.dotRadius;
                        const targetX = right - gap - link.dotRadius;
                        if (targetX <= originX) {
                            return; // Not enough width: draw nothing (the chips still carry the information)
                        }

                        const count = group.hostChipTexts.length;
                        // Branch vertical spacing = binding row height, so endpoints land on chip centers
                        const step = topology.bindingRowHeight;
                        // Origin height comes from originY: aligned with the first binding's center (§A5)
                        const originY = link.originY;

                        ctx.lineWidth = link.lineWidth;
                        ctx.lineCap = "round";

                        for (let i = 0; i < count; ++i) {
                            const color = link.branchColor(group.hostChipTexts[i]);
                            const targetY = step * (i + 0.5);

                            ctx.strokeStyle = color;
                            ctx.fillStyle = color;
                            ctx.beginPath();
                            ctx.moveTo(originX, originY);
                            // Cubic bezier: control points make the link straight, then bend, as sketched
                            const bend = Math.max(Kirigami.Units.gridUnit, (targetX - originX) * 0.45);
                            ctx.bezierCurveTo(originX + bend, originY, targetX - bend, targetY, targetX, targetY);
                            ctx.stroke();

                            // Endpoint drawn as a "socket": ring in the link color, center punched to bg
                            ctx.beginPath();
                            ctx.arc(targetX, targetY, link.dotRadius, 0, Math.PI * 2);
                            ctx.fill();
                            /*
                             * Dual stack (IPv4 + IPv6 wildcard): wrap another ring in a **second color**.
                             *
                             * For a mapping without an explicit host address Docker creates both
                             * `0.0.0.0:<port>` and `[::]:<port>`; the controller already merges them
                             * into one entry, so the double ring shows "two protocol stacks" and it
                             * does not look like a single mapping.
                             */
                            const dualStack = group.dualStackFlags.length > i && group.dualStackFlags[i] === true;
                            if (dualStack) {
                                ctx.strokeStyle = Local.ChartPalette.connectionColor(topology.colorSeed + "|"
                                                                                    + group.containerChipText + "|ipv6");
                                ctx.lineWidth = Math.max(1, link.lineWidth * 0.5);
                                ctx.beginPath();
                                ctx.arc(targetX, targetY, link.dotRadius * 1.32, 0, Math.PI * 2);
                                ctx.stroke();
                                ctx.lineWidth = link.lineWidth;
                            }
                            ctx.fillStyle = Kirigami.Theme.backgroundColor;
                            ctx.beginPath();
                            ctx.arc(targetX, targetY, link.dotRadius * 0.42, 0, Math.PI * 2);
                            ctx.fill();
                        }

                        // The origin is drawn once (shared by all branches). Its color is the **lowest**
                        // branch's: branches are painted in order, lower ones on top, so the origin ring is
                        // continuous with the link through it (user feedback: the topmost color looked off)
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

                /* Wrapper around the container chip: height = one binding row, chip vertically centered —
                   putting its center exactly on the first host binding's center (the origin height) */
                Item {
                    objectName: "portContainerChipSlot"
                    anchors.right: parent.right
                    anchors.rightMargin: Math.max(0, parent.width - topology.chipColumnWidth)
                    anchors.top: parent.top
                    width: containerChip.width
                    height: topology.bindingRowHeight

                    Local.FieldChip {
                        id: containerChip

                        objectName: "portContainerChip"
                        anchors.verticalCenter: parent.verticalCenter
                        // Port text is data; a monospace font makes it easier to compare (§1.5)
                        font.family: "monospace"
                        text: group.containerChipText
                    }
                }

                Column {
                    // Left aligned, right after the node: together with the container side this pulls both
                    // columns toward the middle
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
                            // Same spacing as branch endpoints: chip center = bindingRowHeight * (i + 0.5)
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
