/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later

    The **single** map from status semantics to theme colors / badge types (ARCH_V3 §2.1).

    The input keys come from C++ `Presentation::stateSemanticKey()`:
    positive / neutral / negative / disabled.
    QML never compares container state strings — status semantics belong to C++
    (ARCH_V3 §2.1 layering constraint).

    This is one of the only places in the project allowed to use Kirigami semantic status
    color tokens; the other is ChartPalette (data visualization, no cross-references).
*/

pragma Singleton

import QtQuick
import org.kde.kirigami as Kirigami

QtObject {
    /*!
        Semantic key → status color. For places that cannot use a badge (e.g. the large
        icon on a card's left, the number on a stat tile); badges themselves use StatusChip.
    */
    function color(semanticKey: string): color {
        switch (semanticKey) {
        case "positive":
            return Kirigami.Theme.positiveTextColor;
        case "neutral":
            return Kirigami.Theme.neutralTextColor;
        case "negative":
            return Kirigami.Theme.negativeTextColor;
        default:
            return Kirigami.Theme.disabledTextColor;
        }
    }

    /*!
        Semantic key → Kirigami.Badge.Type.

        Note that Badge's Type names are **color semantics**, not status semantics:
        Positive=green, Warning=orange, Error=red, Information=neutral.
        This map matches the table in ARCH_V3_pre §1.4:
        running=green, paused=orange, stopped=red, unknown/disabled=gray.

        Using Error (red) for "stopped" is a **color convention**, not a statement that the
        container is in an error state; the status text itself always comes from C++ stateText.
    */
    /*!
     * Color of a range-map cell (ARCH_next_ports.md §4.B).
     *
     * An empty `stateKey` = free: dashed border only, no fill — "empty" should look empty.
     * Colors are only an aid: the port number is written in the cell and the page has a
     * written legend.
     */
    function portTileColor(stateKey: string): color {
        switch (stateKey) {
        case "inUse":
            return Kirigami.Theme.positiveBackgroundColor;
        case "reserved":
            return Kirigami.Theme.neutralBackgroundColor;
        case "reservedTaken":
            return Kirigami.Theme.negativeBackgroundColor;
        case "declaredNotPublished":
            // User request: undeclared-free cells need a **light blue** — the gray they had was
            // almost indistinguishable from "free" (Kirigami has no light blue background, so use
            // the highlight color at low opacity)
            return Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g,
                           Kirigami.Theme.highlightColor.b, 0.18);
        default:
            return Kirigami.Theme.backgroundColor;
        }
    }

    /*!
     * Cell border color. Strength order (user request): running = in use > not started >
     * not in use > free.
     *
     * Free is the weakest: a gray outline only, no fill — "empty" should look empty.
     */
    function portTileBorderColor(stateKey: string): color {
        switch (stateKey) {
        case "inUse":
            return Kirigami.Theme.positiveTextColor;
        case "reservedTaken":
            return Kirigami.Theme.negativeTextColor;
        case "reserved":
            return Kirigami.Theme.neutralTextColor;
        default:
            return Kirigami.Theme.disabledTextColor;
        }
    }

    /*! Cell text color (same strength scale as the border). */
    function portTileTextColor(stateKey: string): color {
        switch (stateKey) {
        case "inUse":
            return Kirigami.Theme.positiveTextColor;
        case "reservedTaken":
            return Kirigami.Theme.negativeTextColor;
        case "reserved":
            return Kirigami.Theme.neutralTextColor;
        default:
            return Kirigami.Theme.disabledTextColor;
        }
    }

    function badgeType(semanticKey: string): int {
        switch (semanticKey) {
        case "positive":
            return Kirigami.Badge.Type.Positive;
        case "neutral":
            return Kirigami.Badge.Type.Warning;
        case "negative":
            return Kirigami.Badge.Type.Error;
        default:
            return Kirigami.Badge.Type.Information;
        }
    }

    /*!
        Semantic key → light tint of the badge background, for "large area" cases such as stat
        tiles: using the status color directly would make a whole card too loud, so a very
        faint same-color background is applied only for positive / negative with a non-zero
        value (ARCH_V3_pre §1.9 "tint stopped cards to set them apart").
    */
    function tintColor(semanticKey: string): color {
        switch (semanticKey) {
        case "positive":
            return Qt.rgba(Kirigami.Theme.positiveTextColor.r, Kirigami.Theme.positiveTextColor.g, Kirigami.Theme.positiveTextColor.b, 0.12);
        case "neutral":
            return Qt.rgba(Kirigami.Theme.neutralTextColor.r, Kirigami.Theme.neutralTextColor.g, Kirigami.Theme.neutralTextColor.b, 0.12);
        case "negative":
            return Qt.rgba(Kirigami.Theme.negativeTextColor.r, Kirigami.Theme.negativeTextColor.g, Kirigami.Theme.negativeTextColor.b, 0.12);
        default:
            return Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.06);
        }
    }
}
