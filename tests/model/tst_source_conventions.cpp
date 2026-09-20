/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QtTest>

namespace
{
QString sourceDir()
{
    return QStringLiteral(KCM_DOCKER_SOURCE_DIR);
}

/*! Recursively collect file contents under a directory (`relativePath -> content`). */
QMap<QString, QString> collectFiles(const QString &directory, const QStringList &nameFilters)
{
    QMap<QString, QString> files;
    const QDir root(directory);
    if (!root.exists()) {
        return files;
    }
    QDirIterator iterator(root.absolutePath(), nameFilters, QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            files.insert(root.relativeFilePath(path), QString::fromUtf8(file.readAll()));
        }
    }
    return files;
}

/*!
 * Lines matching a pattern (comments are skipped: naming a token in a comment is not using it).
 */
QStringList linesMatching(const QString &content, const QRegularExpression &pattern)
{
    QStringList hits;
    const QStringList lines = content.split(QLatin1Char('\n'));
    bool insideBlockComment = false;
    for (int index = 0; index < lines.size(); ++index) {
        const QString trimmed = lines.at(index).trimmed();
        if (insideBlockComment) {
            if (trimmed.contains(QLatin1String("*/"))) {
                insideBlockComment = false;
            }
            continue;
        }
        if (trimmed.startsWith(QLatin1String("/*"))) {
            insideBlockComment = !trimmed.contains(QLatin1String("*/"));
            continue;
        }
        if (trimmed.startsWith(QLatin1String("//")) || trimmed.startsWith(QLatin1Char('*'))) {
            continue;
        }
        if (pattern.match(lines.at(index)).hasMatch()) {
            hits.append(QStringLiteral("%1: %2").arg(index + 1).arg(trimmed));
        }
    }
    return hits;
}
} // namespace

/*!
 * Source convention tests (the acceptance criteria of ARCH_V3 §2.1).
 *
 * Review misses these easily: one copy-paste can silently break "status colours have one implementation"
 * or "the read-only boundary", and the UI shows nothing. So they are executable assertions.
 */
class SourceConventionsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void copyActionHasSingleImplementation();
    void qmlPropertiesUseValidSyntax();
    void statusColorsStayInPalettes();
    void mutationsHaveSingleChokePoint();
    void restPathsStayInOneHeader();
    void qmlNeverTalksHttp();
    void kioStaysInHostPathService();
    void externalProcessesStayForbidden();
    void qmlUsesOnlyQmlIdentifiers();
    void pluginMetadataVersionMatchesProject();
    void kcmMetadataIsCompleteForSystemSettings();
};

/*!
 * Copying has one implementation only (CopyButton): it was hand-written 6× across two detail pages.
 */
void SourceConventionsTest::copyActionHasSingleImplementation()
{
    const QMap<QString, QString> files = collectFiles(sourceDir() + QStringLiteral("/src/ui"), {QStringLiteral("*.qml")});
    QVERIFY2(!files.isEmpty(), "no QML files found");

    QStringList offenders;
    for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
        if (it.key() == QLatin1String("components/CopyButton.qml")) {
            continue;
        }
        if (it.value().contains(QLatin1String("copyToClipboard"))) {
            offenders.append(it.key());
        }
    }
    QVERIFY2(offenders.isEmpty(),
             qPrintable(QStringLiteral("copying must go through components/CopyButton.qml, found in: %1").arg(offenders.join(QStringLiteral(", ")))));
}

/*!
 * Semantic status colours live in StatusPalette only (ARCH_V2 §12 / ARCH_V3 §2.1).
 * Charts use ChartPalette's fixed colours; the two must not be mixed.
 */
/*!
 * QML property declarations must use valid syntax (`property bool foo: false`).
 *
 * Paid for with a real incident: `property bool foo: bool = false` only fails at **runtime** with
 * `Error: Invalid write to global property "bool"`, the property is never declared (switches silently
 * stop working), and neither compile nor unit tests catch it — textually it is a valid "expression".
 * This source scan catches it before commit.
 */
void SourceConventionsTest::qmlPropertiesUseValidSyntax()
{
    const QMap<QString, QString> files = collectFiles(sourceDir() + QStringLiteral("/src/ui"), {QStringLiteral("*.qml")});
    QVERIFY(!files.isEmpty());

    // property <type> <name>: <type> = ...  ← the type repeated after the colon is wrong
    static const QRegularExpression suspicious(
        QStringLiteral(R"(^\s*property\s+\w+\s+\w+\s*:\s*(bool|int|real|string|var|double|url|color)\s*=)"));
    QStringList violations;
    for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
        const QStringList lines = it.value().split(QLatin1Char('\n'));
        for (int index = 0; index < lines.size(); ++index) {
            const QRegularExpressionMatch match = suspicious.match(lines.at(index));
            if (match.hasMatch()) {
                violations.append(QStringLiteral("%1:%2  %3").arg(it.key()).arg(index + 1).arg(lines.at(index).trimmed()));
            }
        }
    }
    QVERIFY2(violations.isEmpty(),
             qPrintable(QStringLiteral("invalid QML property declaration (property <type> <name>: <type> = …):\n")
                        + violations.join(QLatin1Char('\n'))));
}

void SourceConventionsTest::statusColorsStayInPalettes()
{
    const QMap<QString, QString> files = collectFiles(sourceDir() + QStringLiteral("/src/ui"), {QStringLiteral("*.qml")});
    const QRegularExpression statusToken(QStringLiteral("(positiveTextColor|neutralTextColor|negativeTextColor|disabledTextColor)"));

    QStringList offenders;
    for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
        if (it.key() == QLatin1String("components/StatusPalette.qml")) {
            continue;
        }
        const QStringList hits = linesMatching(it.value(), statusToken);
        if (!hits.isEmpty()) {
            offenders.append(QStringLiteral("%1 (%2)").arg(it.key(), hits.first()));
        }
    }
    QVERIFY2(offenders.isEmpty(),
             qPrintable(QStringLiteral("status colors must be mapped in components/StatusPalette.qml only:\n%1").arg(offenders.join(QLatin1Char('\n')))));
}

/*!
 * The write choke point (ARCH_V4 §1.5 / §2.2.1).
 *
 * Phase 3 pinned the read-only boundary with "no write verb in production code". Phase 4 opened writes,
 * so the assertion was **not deleted but reshaped**: write verbs and REST paths must both funnel into one
 * place and the UI never touches the transport. Enabling writes stays an explicit, reviewable,
 * test-visible change — only the question moves from "any write verb?" to "where is it?".
 */
void SourceConventionsTest::mutationsHaveSingleChokePoint()
{
    const QMap<QString, QString> sources = collectFiles(sourceDir() + QStringLiteral("/src"), {QStringLiteral("*.cpp"), QStringLiteral("*.h"), QStringLiteral("*.qml")});
    QVERIFY2(!sources.isEmpty(), "no production sources found");

    // Write verbs belong to the transport layer only: the method goes into the request line there
    const QRegularExpression writeVerb(QStringLiteral("\"(POST|PUT|PATCH|DELETE)\""));
    QStringList offenders;
    for (auto it = sources.constBegin(); it != sources.constEnd(); ++it) {
        if (it.key() == QLatin1String("backend/docker_client.cpp")) {
            continue;
        }
        for (const QString &hit : linesMatching(it.value(), writeVerb)) {
            offenders.append(QStringLiteral("%1 → %2").arg(it.key(), hit));
        }
    }
    QVERIFY2(offenders.isEmpty(),
             qPrintable(QStringLiteral("write verbs must stay in backend/docker_client.cpp:\n%1").arg(offenders.join(QLatin1Char('\n')))));
}

/*!
 * REST paths live in docker_api_paths.h only (ARCH_V4 §2.2.1).
 *
 * Scattered paths make "which endpoints does this program call" unanswerable at a glance — with writes
 * in the picture, that question must be answerable at a glance.
 */
void SourceConventionsTest::restPathsStayInOneHeader()
{
    const QMap<QString, QString> sources = collectFiles(sourceDir() + QStringLiteral("/src"), {QStringLiteral("*.cpp"), QStringLiteral("*.h"), QStringLiteral("*.qml")});

    const QRegularExpression restPath(QStringLiteral("QStringLiteral\\(\"/(containers|images|system/df|_ping|version|info)"));
    QStringList offenders;
    for (auto it = sources.constBegin(); it != sources.constEnd(); ++it) {
        if (it.key() == QLatin1String("backend/docker_api_paths.h")) {
            continue;
        }
        for (const QString &hit : linesMatching(it.value(), restPath)) {
            offenders.append(QStringLiteral("%1 → %2").arg(it.key(), hit));
        }
    }
    QVERIFY2(offenders.isEmpty(),
             qPrintable(QStringLiteral("Docker REST paths must be built in backend/docker_api_paths.h only:\n%1").arg(offenders.join(QLatin1Char('\n')))));
}

/*!
 * The UI does not know the transport (ARCH_V4 §1.5): http / verbs / socket paths in QML mean request
 * logic leaked into the UI.
 */
void SourceConventionsTest::qmlNeverTalksHttp()
{
    const QMap<QString, QString> qmlFiles = collectFiles(sourceDir() + QStringLiteral("/src/ui"), {QStringLiteral("*.qml")});
    QVERIFY2(!qmlFiles.isEmpty(), "no QML sources found");

    // Only real transport touches are blocked:
    //  - request APIs (XMLHttpRequest / fetch)
    //  - HTTP method literals, REST path fragments, socket addresses
    // Example addresses (e.g. the mirror placeholder `https://mirror.example.com`) are data, not requests,
    // so `http(s)://` is no longer an offence — that flagged input validation as a leak.
    const QRegularExpression transport(QStringLiteral("(XMLHttpRequest|fetch\\(|\"GET |\"POST|\"DELETE|unix://|/containers/|/images/)"));
    QStringList offenders;
    for (auto it = qmlFiles.constBegin(); it != qmlFiles.constEnd(); ++it) {
        for (const QString &hit : linesMatching(it.value(), transport)) {
            offenders.append(QStringLiteral("%1 → %2").arg(it.key(), hit));
        }
    }
    QVERIFY2(offenders.isEmpty(),
             qPrintable(QStringLiteral("QML must not contain transport details:\n%1").arg(offenders.join(QLatin1Char('\n')))));
}

/*!
 * Opening a host directory is phase 4's only new non-Docker external action, so it must stay in one
 * implementation file instead of spreading over the pages.
 */
void SourceConventionsTest::kioStaysInHostPathService()
{
    const QMap<QString, QString> sources = collectFiles(sourceDir() + QStringLiteral("/src"), {QStringLiteral("*.cpp"), QStringLiteral("*.h"), QStringLiteral("*.qml")});

    const QRegularExpression kioUse(QStringLiteral("(KIO::|#include <KIO/)"));
    QStringList offenders;
    for (auto it = sources.constBegin(); it != sources.constEnd(); ++it) {
        if (it.key().startsWith(QLatin1String("backend/kio_host_path_service"))) {
            continue;
        }
        for (const QString &hit : linesMatching(it.value(), kioUse)) {
            offenders.append(QStringLiteral("%1 → %2").arg(it.key(), hit));
        }
    }
    QVERIFY2(offenders.isEmpty(),
             qPrintable(QStringLiteral("KIO may only be used by backend/kio_host_path_service.*:\n%1").arg(offenders.join(QLatin1Char('\n')))));
}

/*!
 * Boundary of external processes and privilege escalation (ARCH_V3 §1.3 → **ARCH_V5_V8 §1.5.1 relaxed**).
 *
 * Phase 5 added the first restricted privileged component (user-approved; ARCH_V5_V8 §1.5.1: with a
 * system-wide deployment `/etc/docker/daemon.json` is not user-writable, so configuring mirrors needs
 * escalation). The assertion therefore moved from "KAuth banned outright" to a **whitelist**:
 *
 *  - `QProcess` stays banned everywhere (we never shell out; restart goes through systemd D-Bus)
 *  - `KAuth` only in the reviewed privileged files: the client and the helper
 *  - any other file pulling in KAuth / escalation → hard failure
 */
void SourceConventionsTest::externalProcessesStayForbidden()
{
    const QMap<QString, QString> sources = collectFiles(sourceDir() + QStringLiteral("/src"), {QStringLiteral("*.cpp"), QStringLiteral("*.h"), QStringLiteral("*.qml")});
    QVERIFY2(!sources.isEmpty(), "no production sources found");

    // Files allowed to use KAuth (privilege boundary: extending this list must be a reviewed change)
    const QStringList privilegedFiles = {
        QStringLiteral("backend/privileged_config_client.cpp"),
        QStringLiteral("backend/privileged_config_client.h"),
        QStringLiteral("kauth/kcm_docker_helper.cpp"),
        QStringLiteral("kauth/privileged_config_request.cpp"),
        QStringLiteral("kauth/privileged_config_request.h"),
    };

    // Only real process execution is blocked: words like `systemctl` appear in command text we show the
    // user for copying, which we do not execute (restart goes through systemd D-Bus).
    const QRegularExpression externalProcess(QStringLiteral("(QProcess|popen\\(|execv|/bin/sh)"));
    const QRegularExpression privilegeEscalation(QStringLiteral("(KAuth|polkit)"));

    QStringList offenders;
    for (auto it = sources.constBegin(); it != sources.constEnd(); ++it) {
        for (const QString &hit : linesMatching(it.value(), externalProcess)) {
            offenders.append(QStringLiteral("%1 → %2").arg(it.key(), hit));
        }
        if (privilegedFiles.contains(it.key())) {
            continue;
        }
        for (const QString &hit : linesMatching(it.value(), privilegeEscalation)) {
            offenders.append(QStringLiteral("%1 → %2").arg(it.key(), hit));
        }
    }
    QVERIFY2(offenders.isEmpty(),
             qPrintable(QStringLiteral("Kontainer never shells out; privilege escalation is limited to the reviewed helper files:\n%1")
                            .arg(offenders.join(QLatin1Char('\n')))));
}

/*!
 * No C++-side names in QML (`QStringLiteral` / `QString` / …).
 *
 * Seen for real: `i18n("… %1", QStringLiteral("https://…"))` in QML works fine in C++ but throws
 * `ReferenceError: QStringLiteral is not defined`, and only when **that branch actually runs**
 * (adding a blank line triggers validation) — neither compile nor page load shows it. The cost is
 * a feature that merely "seems not to work".
 */
void SourceConventionsTest::qmlUsesOnlyQmlIdentifiers()
{
    const QMap<QString, QString> qmlFiles = collectFiles(sourceDir() + QStringLiteral("/src/ui"), {QStringLiteral("*.qml")});
    QVERIFY2(!qmlFiles.isEmpty(), "no QML sources found");

    const QRegularExpression cppOnly(QStringLiteral("\\b(QStringLiteral|QStringList|QLatin1String|QVariantMap|QStringView|qPrintable|QString::)\\b"));
    const QRegularExpression plainQString(QStringLiteral("\\bQString\\s*\\("));
    QStringList offenders;
    for (auto it = qmlFiles.constBegin(); it != qmlFiles.constEnd(); ++it) {
        for (const QString &hit : linesMatching(it.value(), cppOnly)) {
            offenders.append(QStringLiteral("%1 → %2").arg(it.key(), hit));
        }
        for (const QString &hit : linesMatching(it.value(), plainQString)) {
            offenders.append(QStringLiteral("%1 → %2").arg(it.key(), hit));
        }
    }
    QVERIFY2(offenders.isEmpty(),
             qPrintable(QStringLiteral("C++ identifiers are not available in QML:\n%1").arg(offenders.join(QLatin1Char('\n')))));
}

/*!
 * The plugin metadata version must match `project VERSION`.
 *
 * Once the two drift, the "About" version the user sees no longer matches the built one, and release
 * reviews rarely catch it (nobody compares two files).
 */
void SourceConventionsTest::pluginMetadataVersionMatchesProject()
{
    QFile file(sourceDir() + QStringLiteral("/src/kcm/kcm_docker.json"));
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.fileName()));
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonObject plugin = root.value(QStringLiteral("KPlugin")).toObject();
    QVERIFY2(!plugin.isEmpty(), "the metadata must have a KPlugin section");
    QCOMPARE(plugin.value(QStringLiteral("Version")).toString(), QString::fromLatin1(KCM_DOCKER_VERSION));
}

/*!
 * The module must show up in System Settings, so the metadata must be complete (ARCH §5.18).
 *
 * All requirements **beyond the install path**: System Settings groups and searches by `KPluginMetaData`
 * only, so a missing Id / name / description / icon / keywords / parent category / translation domain
 * each looks "not installed" in its own way:
 *   - no `X-KDE-System-Settings-Parent-Category` → lands in the default group, users cannot find it;
 *   - a misspelled category (e.g. `systemadmin`) → same default group;
 *   - no `X-KDE-Keywords` → searching "container/docker" in System Settings finds nothing;
 *   - no `KLocalizedString.TranslationDomain` → the UI strings stay untranslated.
 */
void SourceConventionsTest::kcmMetadataIsCompleteForSystemSettings()
{
    QFile file(sourceDir() + QStringLiteral("/src/kcm/kcm_docker.json"));
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.fileName()));
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonObject plugin = root.value(QStringLiteral("KPlugin")).toObject();

    // The module id is what the user types in `systemsettings <module>`
    QCOMPARE(plugin.value(QStringLiteral("Id")).toString(), QStringLiteral("kcm_docker"));
    // Name and description need both English and Chinese (this project's UI is bilingual)
    for (const QString &key : {QStringLiteral("Name"), QStringLiteral("Description"),
                               QStringLiteral("Name[zh_CN]"), QStringLiteral("Description[zh_CN]")}) {
        QVERIFY2(!plugin.value(key).toString().isEmpty(), qPrintable(QStringLiteral("missing %1").arg(key)));
    }
    // The icon is required (breeze has folder-docker; a typo shows as a blank icon)
    QCOMPARE(plugin.value(QStringLiteral("Icon")).toString(), QStringLiteral("folder-docker"));

    // The parent category must be one of Plasma 6's real groups, else it lands in the wrong place
    const QStringList knownCategories {QStringLiteral("system-administration"),
                                       QStringLiteral("hardware"),
                                       QStringLiteral("network"),
                                       QStringLiteral("security-privacy"),
                                       QStringLiteral("appearance"),
                                       QStringLiteral("applications"),
                                       QStringLiteral("session"),
                                       QStringLiteral("search"),
                                       QStringLiteral("themes"),
                                       QStringLiteral("windowmanagement"),
                                       QStringLiteral("keyboard"),
                                       QStringLiteral("display"),
                                       QStringLiteral("input-devices"),
                                       QStringLiteral("pointing-devices"),
                                       QStringLiteral("regionalsettings")};
    const QString category = root.value(QStringLiteral("X-KDE-System-Settings-Parent-Category")).toString();
    QVERIFY2(knownCategories.contains(category),
             qPrintable(QStringLiteral("unknown system settings category: '%1'").arg(category)));

    // Search keywords in both languages, so users find it in System Settings
    const QString keywords = root.value(QStringLiteral("X-KDE-Keywords")).toString();
    QVERIFY2(keywords.contains(QLatin1String("docker")), "english keywords are needed for search");
    QVERIFY2(keywords.contains(QStringLiteral("容器")), "chinese keywords are needed for search");

    /*
     * The range map must not use attached tooltips.
     *
     * Real incident (user-reported): `QQC2.ToolTip.text/visible` are **attached properties** — one tooltip
     * per window; the map has dozens of blocks and filter/view switches rebuild the delegates, so "the same
     * container name shows wherever the mouse is, and it survives switching back to the list". The container
     * name now comes from `Accessible.name`, so no ToolTip may appear in the file.
     */
    const QString mapPath = sourceDir() + QStringLiteral("/src/ui/components/HostPortRangeMap.qml");
    QFile mapFile(mapPath);
    QVERIFY2(mapFile.open(QIODevice::ReadOnly), qPrintable(mapPath));
    const QString mapSource = QString::fromUtf8(mapFile.readAll());
    // Code only, not comments (this very rule has to be explained in a comment)
    QString mapCode;
    {
        static const QRegularExpression blockComment(QStringLiteral("/\\*.*?\\*/"), QRegularExpression::DotMatchesEverythingOption);
        QString stripped = mapSource;
        stripped.remove(blockComment);
        const QStringList lines = stripped.split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            const int comment = line.indexOf(QLatin1String("//"));
            mapCode += (comment >= 0 ? line.left(comment) : line);
            mapCode += QLatin1Char('\n');
        }
    }
    QVERIFY2(!mapCode.contains(QLatin1String("ToolTip")),
             "the range map must not use attached tooltips (they leak across delegate rebuilds)");

    // The translation domain must match the one in po/, else no string is translated
    QCOMPARE(root.value(QStringLiteral("KLocalizedString")).toObject().value(QStringLiteral("TranslationDomain")).toString(),
             QStringLiteral("kcm_docker"));
}


QTEST_GUILESS_MAIN(SourceConventionsTest)

#include "tst_source_conventions.moc"
