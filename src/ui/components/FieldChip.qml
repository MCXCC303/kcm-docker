/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Neutral information chip (ARCH_V4 §2.1).

    The split from StatusChip must stay clear:

      StatusChip  means **status** (running / stopped / missing path…), coloured from StatusPalette
      FieldChip   means a **field** (bind / volume / tcp / 80/tcp / rw), always a neutral badge

    Mixing them destroys the colour layer: if port numbers are coloured too, users can no
    longer spot a problem container by colour at a glance (§1.1 status before decoration).

    Usage:

        Components.FieldChip { text: "80/tcp" }
        Components.FieldChip { text: i18n("not published"); muted: true }
*/

import QtQuick

import org.kde.kirigami as Kirigami

Kirigami.Badge {
    id: chip

    /*!
        The chip text comes from the base class (`Kirigami.Badge` → `Label`) `text`.
        Do not redeclare `property string text` here: it shadows the base property, the
        internal rendering keeps using the base (empty) one, and the result is a wordless
        round badge — the property reads correctly while the screen is wrong.
    */

    /*! Dimmed for supplementary information such as "not published", so it does not draw the eye. */
    property bool muted: false

    // Neutral: no status semantics, hence the fixed Information type
    type: Kirigami.Badge.Information
    opacity: chip.muted ? 0.7 : 1.0

    Accessible.role: Accessible.StaticText
    Accessible.name: chip.text
}
