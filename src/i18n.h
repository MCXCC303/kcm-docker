/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QStringList>

namespace Kontainer
{

/*!
 * Translation domain, matching the KCM plugin id (KDE convention: translations install as
 * share/locale/<lang>/LC_MESSAGES/kcm_<name>.mo).
 * po/<lang>/<domain>.po compiles to a .mo of the same name.
 */
inline constexpr auto kTranslationDomain = "kcm_docker";

/*!
 * Set the KDE translation domain (idempotent).
 *
 * All user-facing strings go through KDE i18n (ARCH_V1 §22), which requires the domain to be set
 * first. Call once at KCM startup (and in test main()).
 */
void setupTranslationDomain();

/*!
 * Extra places to look for `kcm_docker.mo` besides `$XDG_DATA_DIRS/share/locale`.
 *
 * Covers running with `QT_PLUGIN_PATH` pointed at the build dir, where the UI came up English
 * because neither the build dir nor the install prefix is in XDG_DATA_DIRS. Only **existing** dirs
 * are returned.
 */
QStringList translationLocaleDirs();

} // namespace Kontainer
