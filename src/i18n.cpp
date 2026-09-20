/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "i18n.h"

#include "i18n.h"

#include <KLocalizedString>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QStringList>

namespace Kontainer
{

QStringList translationLocaleDirs()
{
    /*
     * Translations normally live in `$XDG_DATA_DIRS/share/locale`, but the common dev invocation
     *   QT_PLUGIN_PATH=$PWD/build/bin systemsettings kcm_docker
     * leaves neither the build dir nor the install prefix in XDG_DATA_DIRS, so the UI came up
     * English. Guess candidates from where the plugin may sit and register them explicitly with
     * `KLocalizedString::addDomainLocaleDir()`:
     *
     *   - Qt plugin dir `<prefix>/lib/plugins` → `<prefix>/share/locale` (installed case)
     *   - each `QT_PLUGIN_PATH` entry (dev: `<build>/bin`) → sibling `../locale` and
     *     `../share/locale` (the build tree keeps .mo files in `<build>/locale/`)
     *   - the executable prefix `<prefix>/bin` → `<prefix>/share/locale`
     *
     * Only dirs that really exist are returned, so no invalid path is registered.
     */
    QStringList candidates;
    const auto addFromPrefix = [&candidates](const QString &prefix) {
        if (!prefix.isEmpty()) {
            candidates.append(prefix + QStringLiteral("/share/locale"));
        }
    };

    // Installed plugin dir: <prefix>/lib/plugins (likewise for lib64)
    const QString pluginsPath = QLibraryInfo::path(QLibraryInfo::PluginsPath);
    if (!pluginsPath.isEmpty()) {
        addFromPrefix(QFileInfo(QFileInfo(pluginsPath).absolutePath()).absolutePath());
    }
    // Dev: QT_PLUGIN_PATH points at the build dir
    const QString pluginPathEnv = qEnvironmentVariable("QT_PLUGIN_PATH");
    for (const QString &entry : pluginPathEnv.split(QLatin1Char(':'), Qt::SkipEmptyParts)) {
        const QString base = QFileInfo(entry).absoluteFilePath();
        candidates.append(base + QStringLiteral("/../locale"));
        candidates.append(base + QStringLiteral("/../share/locale"));
    }
    // Prefix of the executable (e.g. ~/kde/usr/bin/kcmshell6)
    addFromPrefix(QFileInfo(QFileInfo(QCoreApplication::applicationDirPath()).absolutePath()).absoluteFilePath());

    QStringList existing;
    for (const QString &candidate : candidates) {
        const QString cleaned = QDir::cleanPath(candidate);
        if (QFileInfo(cleaned).isDir() && !existing.contains(cleaned)) {
            existing.append(cleaned);
        }
    }
    return existing;
}

void setupTranslationDomain()
{
    static bool initialized = false;
    if (initialized) {
        return;
    }
    initialized = true;
    KLocalizedString::setApplicationDomain(QByteArray(kTranslationDomain));

    /*
     * Only register derived dirs that actually contain translations.
     *
     * In KF6 `addDomainLocaleDir()` replaces this domain's search locations, so calling it
     * unconditionally shadows translations otherwise found under `$XDG_DATA_DIRS/share/locale` —
     * with the derivation logic added, tests went English. Verify the dir really holds
     * `<lang>/LC_MESSAGES/kcm_docker.mo`; if none does, leave the normal search path alone.
     */
    const QByteArray domain(kTranslationDomain);
    QStringList usable;
    for (const QString &dir : translationLocaleDirs()) {
        const QDir localeDir(dir);
        const QStringList languages = localeDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &language : languages) {
            if (QFileInfo::exists(dir + QLatin1Char('/') + language + QStringLiteral("/LC_MESSAGES/") + QString::fromLatin1(kTranslationDomain)
                                  + QStringLiteral(".mo"))) {
                usable.append(dir);
                break;
            }
        }
    }
    for (const QString &dir : usable) {
        KLocalizedString::addDomainLocaleDir(domain, dir);
    }
}

} // namespace Kontainer
