/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

namespace Kontainer
{

/*!
 * Register the QML types (org.kde.kcm.docker).
 *
 * The KCM and the QML load test share this registration code so the two cannot drift apart.
 * Idempotent: repeated calls have no effect (Qt ignores duplicate registrations).
 */
void registerKontainerQmlTypes();

} // namespace Kontainer
