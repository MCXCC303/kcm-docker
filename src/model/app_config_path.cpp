/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/app_config_path.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>

namespace Kontainer
{

QString defaultAppConfigPath(const QString &configDir)
{
    const QString directory = configDir.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
        : configDir;
    const QString current = directory + QStringLiteral("/kcm_dockerrc");
    const QString legacy = directory + QStringLiteral("/kontainerrc");

    // Migrate once: new file absent and old file present -> copy it over (old file kept)
    if (!QFile::exists(current) && QFile::exists(legacy)) {
        QDir().mkpath(directory);
        QFile::copy(legacy, current);
    }
    return current;
}

} // namespace Kontainer
