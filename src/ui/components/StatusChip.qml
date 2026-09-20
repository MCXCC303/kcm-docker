/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Status badge (ARCH_V3 §2.1 / ARCH_V3_pre §1.3):

    Presents status as **icon + color + text** triple encoding, and forbids pages from
    building their own if-else colors (colors and badge types are mapped once in
    Local.StatusPalette).

    Usage (status semantics and icon names both come from C++; QML does not test status strings):

        Components.StatusChip {
            semanticKey: Kontainer.Presentation.stateSemanticKey(stateKey, healthKey)
            iconName: Kontainer.Presentation.stateIconName(stateKey)
            text: stateText
        }
*/

import QtQuick

import org.kde.kirigami as Kirigami

import "." as Local

Kirigami.Badge {
    id: chip

    /*! Semantic key: positive / neutral / negative / disabled. */
    required property string semanticKey
    /*! Icon name; empty means no icon (e.g. a container without a health check). */
    property string iconName: ""

    type: Local.StatusPalette.badgeType(chip.semanticKey)

    // §1.6/§1.8: the badge always has text and the icon is second encoding, so color carries no semantics
    icon.name: chip.iconName
    icon.width: Kirigami.Units.iconSizes.sizeForLabels
    icon.height: Kirigami.Units.iconSizes.sizeForLabels

    Accessible.role: Accessible.StaticText
    Accessible.name: chip.text
    Accessible.description: chip.iconName
}
