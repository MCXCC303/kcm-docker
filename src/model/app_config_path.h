/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QString>

namespace Kontainer
{

/*!
 * Config file path for this module (`~/.config/kcm_dockerrc`), plus a one-time **legacy name migration**.
 *
 * The app was renamed `kontainer` -> `kcm-docker` (the old name was taken by another app), so the config
 * file moves too; existing mount presets and command history must survive, so on first run the old file is
 * **copied** (not moved: keeping it lets users roll back to the old version).
 *
 * @param configDir Config directory; empty uses the `QStandardPaths` location (tests pass a temp dir).
 */
QString defaultAppConfigPath(const QString &configDir = QString());

} // namespace Kontainer
