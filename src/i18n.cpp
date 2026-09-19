/*
    SPDX-FileCopyrightText: 2026 kontainer developers
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
     * 译文的常规查找路径是 `$XDG_DATA_DIRS/share/locale`。但开发时常见的用法是
     *   QT_PLUGIN_PATH=$PWD/build/bin systemsettings kcm_docker
     * 这时 XDG_DATA_DIRS 里没有构建目录/安装前缀，界面就变成英文（实测反馈）。
     * 因此这里按"插件可能被放在哪里"补几个候选目录，用
     * `KLocalizedString::addDomainLocaleDir()` 显式登记：
     *
     *   - Qt 的插件目录 `<prefix>/lib/plugins` → `<prefix>/share/locale`（安装后的常规情形）
     *   - `QT_PLUGIN_PATH` 的每一项（开发时指向 `<build>/bin`）→ 同级的 `../locale`
     *     与 `../share/locale`（构建树里 .mo 就放在 `<build>/locale/`）
     *   - 可执行文件所在前缀 `<prefix>/bin` → `<prefix>/share/locale`
     *
     * 只返回**确实存在**的目录，避免把无效路径注册进去。
     */
    QStringList candidates;
    const auto addFromPrefix = [&candidates](const QString &prefix) {
        if (!prefix.isEmpty()) {
            candidates.append(prefix + QStringLiteral("/share/locale"));
        }
    };

    // 安装后的插件目录：<prefix>/lib/plugins（lib64 同理）
    const QString pluginsPath = QLibraryInfo::path(QLibraryInfo::PluginsPath);
    if (!pluginsPath.isEmpty()) {
        addFromPrefix(QFileInfo(QFileInfo(pluginsPath).absolutePath()).absolutePath());
    }
    // 开发：QT_PLUGIN_PATH 指向构建目录
    const QString pluginPathEnv = qEnvironmentVariable("QT_PLUGIN_PATH");
    for (const QString &entry : pluginPathEnv.split(QLatin1Char(':'), Qt::SkipEmptyParts)) {
        const QString base = QFileInfo(entry).absoluteFilePath();
        candidates.append(base + QStringLiteral("/../locale"));
        candidates.append(base + QStringLiteral("/../share/locale"));
    }
    // 可执行文件所在的前缀（例如 ~/kde/usr/bin/kcmshell6）
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
     * 只在**派生目录里确实有译文**时才登记它们。
     *
     * `addDomainLocaleDir()` 会换掉这个域的查找位置（KF6 的行为），无条件调用会把
     * `$XDG_DATA_DIRS/share/locale` 里本来能找到的译文挤掉——实测表现就是
     * "加了推导逻辑之后，测试环境里反而变成英文"。所以先确认那个目录里真的有
     * `<lang>/LC_MESSAGES/kcm_docker.mo`；一个都没有就完全不动常规查找路径。
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
