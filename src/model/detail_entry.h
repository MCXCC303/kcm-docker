/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QString>

namespace Kontainer
{

/*!
 * One read-only display row on the detail pages (ARCH_V2 §7/§8).
 *
 * Shared by the isomorphic lists (ports / networks / mounts / labels / env vars), so none of them needs
 * its own model and Docker JSON never reaches QML directly (ARCH_V1 §12.2).
 */
struct DetailEntry {
    QString label; /*!< Primary text: network name / mount target / variable name / tag */
    QString value; /*!< Secondary text: IP / source path / variable value / digest */
    QString detail; /*!< Optional third line: MAC / mode / extra note */
    QString entryKey; /*!< Stable key: bind/volume/tmpfs, tcp/udp, healthy… (drives QML icons and semantics) */
    /*!
     * State key of the related container (`running` / `paused` / `exited` …).
     *
     * Both the image's related-containers list and the network members list show a **state icon**
     * (user feedback: keep the two consistent), so state gets its own field instead of reusing entryKey.
     */
    QString stateKey;
    /*! Object to navigate to on click (currently only a container id); empty = not clickable. */
    QString target;

    /*!
     * Value comparison: on a silent refresh (unchanged data) the model emits nothing (ARCH_V2 §32/§34).
     * Detail lists used to reset unconditionally on every refresh, so QML Repeaters destroyed and rebuilt
     * their delegates every 5 seconds -- the breeding ground for dangling items during layout polish.
     */
    friend bool operator==(const DetailEntry &lhs, const DetailEntry &rhs)
    {
        // New fields must join the comparison too: otherwise a state-only change never refreshes (stale icon)
        return lhs.label == rhs.label && lhs.value == rhs.value && lhs.detail == rhs.detail && lhs.entryKey == rhs.entryKey
            && lhs.stateKey == rhs.stateKey && lhs.target == rhs.target;
    }
};

} // namespace Kontainer
