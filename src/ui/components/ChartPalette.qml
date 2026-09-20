/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Data-visualization palette (ARCH_V3 §2.4 / ARCH_V3_pre §1.4).

    §1.4 lists two exceptions to the semantic-colour-only rule: sparkline/trend series
    (CPU / memory / network / block IO) must be mutually distinguishable, and the storage
    stacked bar is neutral data display. So fixed low-saturation values are defined per
    light/dark theme instead of reusing status tokens like positiveTextColor /
    negativeTextColor. StatusPalette is never referenced here: status semantics and data
    series stay independent, or "green" would mean both "running" and "network traffic".

    ## Contrast

    Every value was WCAG-checked against the target theme background (3:1 required for
    non-text graphics; a stricter 4.5:1 was used here):

    | theme | reference background | lowest series contrast |
    | --- | --- | --- |
    | light | `#eff0f1` (Breeze Light view background) | 5.5:1 |
    | dark | `#232629` (Breeze Dark view background, §1.8) | 6.5:1 |

    The storage ramp (one hue, four lightness steps) stays ≥ 4.5:1 on both themes with
    adjacent steps only 1.19–1.28:1 apart, so the stacked bar **must** draw a
    background-coloured divider between segments and provide a "colour + text + value"
    triple-encoded legend — colour alone cannot separate them (§1.8).

    ## Light/dark detection

    Kirigami 6.30 exposes **no** `colorScheme` / `Theme.Light` / `Theme.Dark` QML API
    (measured: `Kirigami.Theme.colorScheme` and `Platform.Theme.colorScheme` are both
    undefined, and the qmltypes has no such property). Hence an enum-free test: compare
    the relative luminance of background vs text — text always contrasts with the
    background, so the brighter one identifies the scheme. Holds for light, dark and
    high-contrast themes.
*/

pragma Singleton

import QtQuick

import org.kde.kirigami as Kirigami
import org.kde.kcm.docker as Kontainer

QtObject {
    /*!
        Single sRGB channel → linear luminance component (WCAG 2.x).
    */
    function linearize(channel: real): real {
        return channel <= 0.04045 ? channel / 12.92 : Math.pow((channel + 0.055) / 1.055, 2.4);
    }

    /*! Relative luminance of a colour (0 = black, 1 = white). */
    function relativeLuminance(c: color): real {
        return 0.2126 * linearize(c.r) + 0.7152 * linearize(c.g) + 0.0722 * linearize(c.b);
    }

    /*! WCAG contrast ratio of two colours, for tests and validation. */
    function contrastRatio(a: color, b: color): real {
        const la = relativeLuminance(a);
        const lb = relativeLuminance(b);
        const lighter = Math.max(la, lb);
        const darker = Math.min(la, lb);
        return (lighter + 0.05) / (darker + 0.05);
    }

    /*! Whether the current colour scheme is dark. */
    readonly property bool darkScheme: relativeLuminance(Kirigami.Theme.backgroundColor) < relativeLuminance(Kirigami.Theme.textColor)

    /*! Reference backgrounds for validation (Breeze Light / Dark view background). */
    readonly property color lightReferenceBackground: "#eff0f1"
    readonly property color darkReferenceBackground: "#232629"

    /* ---------------- Trend chart series colours ---------------- */

    readonly property color cpuSeries: darkScheme ? "#6fb3ff" : "#0b5d9e"
    readonly property color memorySeries: darkScheme ? "#f0a35e" : "#8a5300"
    readonly property color networkSeries: darkScheme ? "#6fcf97" : "#186b3a"
    readonly property color blockSeries: darkScheme ? "#b79cf0" : "#6b3fa0"

    /* ---------------- Storage stacked bar (one hue, four lightness steps) ---------------- */

    readonly property color storageImages: darkScheme ? "#4098e0" : "#12456e"
    readonly property color storageContainers: darkScheme ? "#65ace6" : "#165589"
    readonly property color storageVolumes: darkScheme ? "#87beeb" : "#1a64a0"
    readonly property color storageBuildCache: darkScheme ? "#a9d0f1" : "#1d70b4"

    /* ---------------- Port mapping topology (ARCH_V4 §2.1.2) ---------------- */

    /*!
        Neutral colour for topology links (fallback when the seed is empty).
        This is a **structural graphic**, neither data series nor status: so it uses its
        own neutral pair, contrasting with the background on both schemes (§1.8 / AA).
    */
    readonly property color topologyLink: darkScheme ? "#9aa4ad" : "#4a545e"

    /*!
        Selectable topology link colours (one per link, see `connectionColor()`).

        Requirements: mutually distinguishable, ≥ 3:1 against both theme backgrounds,
        and **semantically meaningless** — the link colour only makes one container's
        graph look like a unit, the port and binding text carries the information (§1.1:
        colour must not be the only means of distinction). Deliberately avoids
        StatusPalette's red/amber/green semantics, using low-saturation mid hues.
    */
    readonly property var topologyConnectionColors: darkScheme
        ? ["#d98b8b", "#d9b06a", "#8fc98f", "#7fc4c4", "#8fb3e0", "#b79cd9"]
        : ["#a4504f", "#8a6a1f", "#3f6b3f", "#2f6b6b", "#33557f", "#5c4a80"]

    /*!
        Picks a link colour from a seed: the same seed always gets the same colour.

        The seed is usually a container id (FNV-1a modulo in
        `Presentation::connectionColorIndex`), so refresh, page reopen and theme switch
        never recolour a container's link — a theme switch selects that theme's palette.
    */
    function connectionColor(seed: string): color {
        if (!seed) {
            return topologyLink;
        }
        const palette = topologyConnectionColors;
        return palette[Kontainer.Presentation.connectionColorIndex(seed, palette.length)];
    }

    /*! Topology node (container / host) background and border. */
    readonly property color topologyNodeBackground: darkScheme ? "#31363b" : "#e6e9ec"
    readonly property color topologyNodeBorder: darkScheme ? "#4b5157" : "#c2c7cc"

    /*!
        Divider colour between segments.
        Adjacent storage ramp steps differ by only ~1.2:1, too little to distinguish
        reliably by colour, so the stacked bar must draw this divider (§1.8: colour must
        not be the only means of distinction).
    */
    readonly property color storageSeparator: Kirigami.Theme.backgroundColor
}
