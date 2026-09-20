/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "i18n.h"
#include "model/presentation.h"

#include <KLocalizedString>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

using namespace Kontainer;

/*!
 * i18n consistency tests.
 *
 * Guards against a trap already hit: the KCM's translation domain must equal the plugin id (KDE
 * convention), and TranslationDomain in the metadata, the po/ directory name and
 * kTranslationDomain must all agree, otherwise the UI silently falls back to English (QML and C++
 * may even show half Chinese, half English).
 */
class I18nConsistencyTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void domainMatchesPluginId();
    void metadataUsesSameDomain();
    void translationsExistAndAreComplete();
    void templateMatchesSources();
    void translationsLoadAtRuntime();
    void developmentPluginPathFindsTheCatalog();
    void noUnwrappedUiStrings();
    void translationsDoNotInventArguments();
    void noFuzzyEntriesInTheCatalog();
};

namespace
{
QString sourceDir()
{
    return QStringLiteral(KCM_DOCKER_SOURCE_DIR);
}
} // namespace

void I18nConsistencyTest::domainMatchesPluginId()
{
    // Translations install to share/locale/<lang>/LC_MESSAGES/<domain>.mo and the KCM's QML side
    // uses the plugin id as its domain, so the two must match.
    QCOMPARE(QString::fromLatin1(kTranslationDomain), QStringLiteral("kcm_docker"));
}

void I18nConsistencyTest::metadataUsesSameDomain()
{
    const QString path = sourceDir() + QStringLiteral("/src/kcm/kcm_docker.json");
    QFile file(path);
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(path));

    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QString domain = root.value(QStringLiteral("KLocalizedString")).toObject().value(QStringLiteral("TranslationDomain")).toString();
    QCOMPARE(domain, QString::fromLatin1(kTranslationDomain));
}

void I18nConsistencyTest::translationsExistAndAreComplete()
{
    const QString domain = QString::fromLatin1(kTranslationDomain);
    const QString poDirectory = sourceDir() + QStringLiteral("/po/zh_CN");
    const QString poFile = poDirectory + QLatin1Char('/') + domain + QStringLiteral(".po");
    QVERIFY2(QFile::exists(poFile), qPrintable(QStringLiteral("missing translation file: %1").arg(poFile)));

    QFile file(poFile);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QString content = QString::fromUtf8(file.readAll());

    // Check entry by entry: a non-empty msgid with an empty msgstr (or msgstr[0]) is untranslated
    // Note: skip the header metadata entry, whose msgid is ""
    const QRegularExpression emptyTranslation(QStringLiteral("^msgstr(?:\\[0\\])? \"\"$"), QRegularExpression::MultilineOption);
    const QStringList entries = content.split(QStringLiteral("\n\n"));
    int untranslated = 0;
    for (const QString &entry : entries) {
        if (!entry.contains(QStringLiteral("msgid ")) || entry.contains(QStringLiteral("msgid \"\""))) {
            continue;
        }
        if (emptyTranslation.match(entry).hasMatch()) {
            ++untranslated;
        }
    }
    QVERIFY2(untranslated == 0, qPrintable(QStringLiteral("%1 untranslated entries in %2").arg(untranslated).arg(domain)));
}

/*!
 * ARCH_V3 §2.6: user-visible UI text must not be a plain string literal.
 *
 * Only properties that definitely carry user-visible text are checked (text / title / accessible
 * names / tooltips ...), not non-text ones like objectName, icon.name, source or font.family, so
 * false positives are rare.
 *
 * When a literal really is needed (e.g. a pure symbol placeholder), add the comment
 * `i18n-lint: allow <reason>` on that line, marking the exception as reviewed rather than missed.
 */
namespace
{

/*!
 * Replace comments with spaces (keeping line numbers and string contents intact).
 *
 * Why: the lint is a per-line regex, and doc comments often contain example code (e.g. a component
 * usage writing `text: "80/tcp"`). That is not real UI text, but unfiltered it counts as a
 * violation -- and once an assertion starts misfiring, the next person switches it off instead of
 * fixing it.
 */
QString stripComments(const QString &content)
{
    QString out;
    out.reserve(content.size());
    bool inBlock = false;
    bool inLine = false;
    bool inString = false;
    QChar quote;

    for (int i = 0; i < content.size(); ++i) {
        const QChar c = content.at(i);
        const QChar next = i + 1 < content.size() ? content.at(i + 1) : QChar();

        if (inLine) {
            if (c == QLatin1Char('\n')) {
                inLine = false;
                out.append(c);
            } else {
                out.append(QLatin1Char(' '));
            }
            continue;
        }
        if (inBlock) {
            if (c == QLatin1Char('*') && next == QLatin1Char('/')) {
                inBlock = false;
                out.append(QLatin1String("  "));
                ++i;
            } else {
                out.append(c == QLatin1Char('\n') ? c : QLatin1Char(' '));
            }
            continue;
        }
        if (inString) {
            out.append(c);
            if (c == QLatin1Char('\\') && i + 1 < content.size()) {
                out.append(content.at(i + 1));
                ++i;
            } else if (c == quote) {
                inString = false;
            }
            continue;
        }
        if (c == QLatin1Char('/') && next == QLatin1Char('/')) {
            inLine = true;
            out.append(QLatin1String("  "));
            ++i;
            continue;
        }
        if (c == QLatin1Char('/') && next == QLatin1Char('*')) {
            inBlock = true;
            out.append(QLatin1String("  "));
            ++i;
            continue;
        }
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            inString = true;
            quote = c;
        }
        out.append(c);
    }
    return out;
}

} // namespace

/*!
 * The template must cover every translatable string in the sources.
 *
 * Why: phase five dropped over 90 of them at once (new UI text never reached `po/`), and a missing
 * string shows up as mixed English and Chinese -- visible only if someone really switches to
 * Chinese. This runs xgettext (the same command `po/README.md` gives translators) and compares only
 * the **msgctxt + msgid + msgid_plural** set, not line references (those change with every code edit
 * and only add noise).
 */
namespace
{

/*!
 * Parse a po/pot into a set of `msgctxt\x1fmsgid\x1fmsgid_plural` strings.
 *
 * Only what this test needs: split on blank lines -> take three fields -> strip quotes and
 * continuations. No general parser (that would need a full gettext implementation, and the input
 * here is our own file).
 */
QSet<QString> potKeys(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QString content = QString::fromUtf8(file.readAll());

    const auto field = [](const QStringList &block, const QString &key) {
        QString value;
        bool collecting = false;
        for (const QString &line : block) {
            if (line.startsWith(key + QLatin1Char(' '))) {
                collecting = true;
                value += line.mid(key.size() + 1).trimmed().mid(1, line.trimmed().size() - 2);
            } else if (collecting && line.startsWith(QLatin1Char('"'))) {
                value += line.trimmed().mid(1, line.trimmed().size() - 2);
            } else if (collecting) {
                break;
            }
        }
        return value;
    };

    QSet<QString> keys;
    const QStringList blocks = content.split(QStringLiteral("\n\n"));
    for (const QString &raw : blocks) {
        if (raw.startsWith(QLatin1String("#~"))) {
            continue; // obsolete entry
        }
        const QStringList block = raw.split(QLatin1Char('\n'));
        const QString id = field(block, QStringLiteral("msgid"));
        if (id.isEmpty()) {
            continue; // header metadata entry
        }
        keys.insert(field(block, QStringLiteral("msgctxt")) + QChar(0x1f) + id + QChar(0x1f)
                    + field(block, QStringLiteral("msgid_plural")));
    }
    return keys;
}

} // namespace

void I18nConsistencyTest::templateMatchesSources()
{
    const QString xgettext = QStandardPaths::findExecutable(QStringLiteral("xgettext"));
    if (xgettext.isEmpty()) {
        QSKIP("xgettext is not installed; cannot verify the translation template");
    }

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString extracted = dir.filePath(QStringLiteral("extracted.pot"));

    QStringList sources;
    QDirIterator iterator(sourceDir() + QStringLiteral("/src"),
                          {QStringLiteral("*.cpp"), QStringLiteral("*.h"), QStringLiteral("*.qml")},
                          QDir::Files,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        sources.append(iterator.next());
    }
    QVERIFY(!sources.isEmpty());

    // Match the command written for translators in po/README.md (otherwise both sides see different sets)
    QStringList arguments{QStringLiteral("--language=C++"),
                          QStringLiteral("--from-code=UTF-8"),
                          QStringLiteral("--keyword=i18n"),
                          QStringLiteral("--keyword=i18nc:1c,2"),
                          QStringLiteral("--keyword=i18np:1,2"),
                          QStringLiteral("--keyword=i18ncp:1c,2,3"),
                          QStringLiteral("--package-name=kontainer"),
                          QStringLiteral("--output=") + extracted};
    arguments += sources;
    QProcess process;
    process.start(xgettext, arguments);
    QVERIFY2(process.waitForFinished(60000), "xgettext did not finish");
    QCOMPARE(process.exitCode(), 0);

    const QSet<QString> fromSources = potKeys(extracted);
    const QSet<QString> fromTemplate = potKeys(sourceDir() + QStringLiteral("/po/kcm_docker.pot"));
    QVERIFY(!fromTemplate.isEmpty());

    QStringList missing;
    for (const QString &key : fromSources) {
        if (!fromTemplate.contains(key)) {
            missing.append(key.section(QChar(0x1f), 1, 1));
        }
    }
    missing.sort();
    QVERIFY2(missing.isEmpty(),
             qPrintable(QStringLiteral("po/kcm_docker.pot is out of date; missing %1 string(s):\n%2")
                            .arg(missing.size())
                            .arg(missing.mid(0, 10).join(QLatin1Char('\n')))));

    QStringList stale;
    for (const QString &key : fromTemplate) {
        if (!fromSources.contains(key)) {
            stale.append(key.section(QChar(0x1f), 1, 1));
        }
    }
    stale.sort();
    QVERIFY2(stale.isEmpty(),
             qPrintable(QStringLiteral("po/kcm_docker.pot has %1 string(s) no longer in the sources:\n%2")
                            .arg(stale.size())
                            .arg(stale.mid(0, 10).join(QLatin1Char('\n')))));
}

/*!
 * Can ki18n really read the translations back?
 *
 * The first three cases check "is it in the file"; this one checks "what will be displayed at
 * runtime": any mistake in domain, language, install directory or .mo content makes the UI silently
 * fall back to English, a failure invisible in CI (unless someone really switches to Chinese and
 * looks).
 */
void I18nConsistencyTest::translationsLoadAtRuntime()
{
    const QString moFile = QStringLiteral(KCM_DOCKER_BUILD_DIR) + QStringLiteral("/locale/zh_CN/LC_MESSAGES/kcm_docker.mo");
    if (!QFile::exists(moFile)) {
        QSKIP("the compiled .mo does not exist yet; build the kcm_docker target first");
    }

    // Pin the language explicitly: the test must not follow the developer's LANG
    // (XDG_DATA_DIRS and LANGUAGE are injected by tests/CMakeLists.txt, and QStandardPaths
    //  initializes only once per process, so qputenv inside the test body is too late)
    KLocalizedString::setLanguages({QStringLiteral("zh_CN")});
    setupTranslationDomain();

    // Pick three covering different sources (QML/C++, plain entry, plural entry)
    QCOMPARE(i18n("Save"), QStringLiteral("保存"));
    QCOMPARE(i18n("Unlock to edit"), QStringLiteral("解锁以编辑"));
    QCOMPARE(i18ncp("@info image layer count", "Layers (%1)", "Layers (%1)", 3), QStringLiteral("层（3）"));

    // Detail-page related containers / network members take state text from the same catalog as the list
    Presentation presentation;
    QCOMPARE(presentation.stateText(QStringLiteral("running")), QStringLiteral("运行中"));
    QCOMPARE(presentation.stateText(QStringLiteral("paused")), QStringLiteral("已暂停"));

    // A string without a translation must be returned as is: an empty string would show blank buttons
    const char *untranslated = "this string is intentionally not translated";
    QCOMPARE(i18n(untranslated), QString::fromLatin1(untranslated));
}

void I18nConsistencyTest::noUnwrappedUiStrings()
{
    // Plain string, not a raw string: the regex itself ends with )" and raw strings trip over delimiters
    const QRegularExpression textProperty(
        // Qualified prefixes are allowed (Kirigami.FormData.label, Accessible.name, QQC2.ToolTip.text),
        // but the leaf must be a property that carries user-visible text, so icon.name never misfires
        QStringLiteral("(?:^|[ \\t])(?:[A-Za-z_][A-Za-z0-9_]*\\.)*(text|title|explanation|placeholderText|displayText|Accessible\\.name|Accessible\\.description|ToolTip\\.text|FormData\\.label)[ \\t]*:[ \\t]*\"([^\"]*)\""));
    const QRegularExpression userVisible(QStringLiteral("[A-Za-z\\x{4e00}-\\x{9fff}]"));

    QStringList violations;
    const QDir uiDirectory(sourceDir() + QStringLiteral("/src/ui"));
    QDirIterator iterator(uiDirectory.absolutePath(), {QStringLiteral("*.qml")}, QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        // Two views of the content: raw to spot the exemption marker (it lives in a comment, which
        // stripping would hide), stripped to match real property assignments, not doc-comment examples
        const QString rawContent = QString::fromUtf8(file.readAll());
        const QStringList rawLines = rawContent.split(QLatin1Char('\n'));
        const QStringList lines = stripComments(rawContent).split(QLatin1Char('\n'));
        for (int index = 0; index < lines.size(); ++index) {
            const QString &line = lines.at(index);
            if (index < rawLines.size() && rawLines.at(index).contains(QLatin1String("i18n-lint: allow"))) {
                continue;
            }
            const QRegularExpressionMatch match = textProperty.match(line);
            if (!match.hasMatch()) {
                continue;
            }
            const QString value = match.captured(2);
            if (value.isEmpty() || !userVisible.match(value).hasMatch()) {
                continue;
            }
            violations.append(QStringLiteral("%1:%2  %3").arg(uiDirectory.relativeFilePath(path)).arg(index + 1).arg(line.trimmed()));
        }
    }

    QVERIFY2(violations.isEmpty(),
             qPrintable(QStringLiteral("user-visible text must go through i18n():\n%1").arg(violations.join(QLatin1Char('\n')))));
}

/*!
 * Translations must also work when developing via `QT_PLUGIN_PATH=<build>/bin` (no XDG_DATA_DIRS).
 *
 * User-reported: `QT_PLUGIN_PATH=$PWD/build/bin systemsettings kcm_docker` showed English in a
 * Chinese locale, because translations are looked up only in `$XDG_DATA_DIRS/share/locale` and
 * neither the build directory nor the install prefix is listed there. `translationLocaleDirs()` is
 * the candidate list added for this: `<build>/locale` and `<build>/share/locale` derived from
 * `QT_PLUGIN_PATH`, plus `<prefix>/share/locale` from the Qt plugin directory and the executable
 * location. The temporary directory pins "derived correctly, only existing directories returned".
 */
void I18nConsistencyTest::developmentPluginPathFindsTheCatalog()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // Simulate a build tree: <tmp>/bin (plugins) and <tmp>/locale/zh_CN/LC_MESSAGES/kcm_docker.mo
    QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("bin"))));
    const QString localeDir = dir.filePath(QStringLiteral("locale/zh_CN/LC_MESSAGES"));
    QVERIFY(QDir().mkpath(localeDir));
    QFile catalog(localeDir + QStringLiteral("/kcm_docker.mo"));
    QVERIFY(catalog.open(QIODevice::WriteOnly));
    catalog.write("dummy"); // existing is enough: this tests directory derivation, not translation content
    catalog.close();

    const QByteArray previous = qgetenv("QT_PLUGIN_PATH");
    qputenv("QT_PLUGIN_PATH", dir.filePath(QStringLiteral("bin")).toUtf8());
    const QStringList dirs = translationLocaleDirs();
    qputenv("QT_PLUGIN_PATH", previous);

    QVERIFY2(dirs.contains(dir.filePath(QStringLiteral("locale"))),
             qPrintable(QStringLiteral("missing build-tree locale dir, got: %1").arg(dirs.join(QLatin1Char(' ')))));
    // Non-existent directories must not be registered (do not feed invalid paths to KLocalizedString)
    QVERIFY(!dirs.contains(dir.filePath(QStringLiteral("share/locale"))));

    // Negative direction: a directory without a .mo does not count (the list holds only existing ones)
    QTemporaryDir empty;
    QVERIFY(empty.isValid());
    qputenv("QT_PLUGIN_PATH", empty.filePath(QStringLiteral("bin")).toUtf8());
    const QStringList emptyDirs = translationLocaleDirs();
    qputenv("QT_PLUGIN_PATH", previous);
    QVERIFY(!emptyDirs.contains(empty.filePath(QStringLiteral("locale"))));
}


/*!
 * Translations must not invent argument placeholders.
 *
 * Background (real incident): while bulk-filling zh_CN translations a script replaced only the FIRST
 * line of each `msgstr` and left the old continuation lines, so the Chinese became "two sentences
 * glued together" and `%!I(18N_ARGUMENT_MISSING)` showed up in a tooltip (caught in a user
 * screenshot). Rule: every `%N` in a `msgstr` must also appear in the matching `msgid` /
 * `msgid_plural`.
 */
void I18nConsistencyTest::translationsDoNotInventArguments()
{
    const QString poFile = sourceDir() + QStringLiteral("/po/zh_CN/kcm_docker.po");
    QFile file(poFile);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QString content = QString::fromUtf8(file.readAll());

    // Collect a field's full text, continuation lines included
    const auto fieldText = [](const QString &entry, const QString &key) {
        QString collected;
        bool collecting = false;
        const QStringList lines = entry.split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            if (line.startsWith(key + QLatin1Char(' '))) {
                collecting = true;
            } else if (!line.startsWith(QLatin1Char('"'))) {
                if (collecting) {
                    break;
                }
                continue;
            }
            if (collecting) {
                QString value = line.mid(line.indexOf(QLatin1Char('"')) + 1);
                value.chop(1); // drop the trailing quote
                collected += value;
            }
        }
        return collected;
    };
    const auto argumentsIn = [](const QString &text) {
        QSet<QString> found;
        static const QRegularExpression pattern(QStringLiteral("%\\d+"));
        auto it = pattern.globalMatch(text);
        while (it.hasNext()) {
            found.insert(it.next().captured());
        }
        return found;
    };

    int offenders = 0;
    const QStringList entries = content.split(QStringLiteral("\n\n"));
    for (const QString &entry : entries) {
        const QString msgid = fieldText(entry, QStringLiteral("msgid"));
        if (msgid.isEmpty()) {
            continue;
        }
        const QSet<QString> allowed = argumentsIn(msgid) | argumentsIn(fieldText(entry, QStringLiteral("msgid_plural")));
        const QStringList keys {QStringLiteral("msgstr"), QStringLiteral("msgstr[0]"), QStringLiteral("msgstr[1]"),
                                QStringLiteral("msgstr[2]")};
        for (const QString &key : keys) {
            const QString msgstr = fieldText(entry, key);
            if (msgstr.isEmpty()) {
                continue;
            }
            const QSet<QString> extra = argumentsIn(msgstr) - allowed;
            if (!extra.isEmpty()) {
                ++offenders;
                qWarning() << "translation invents arguments" << extra << "for msgid" << msgid.left(60);
            }
        }
    }
    QVERIFY2(offenders == 0,
             qPrintable(QStringLiteral("%1 translated entries use %N that the source string does not have").arg(offenders)));
}


/*!
 * No `#, fuzzy` markers may remain in the `.po`.
 *
 * Real incident (users twice saw "these two strings are not translated"): `msgmerge` marks changed
 * entries fuzzy, and **fuzzy entries are not compiled into the `.mo`** -- so `i18n()` silently falls
 * back to English and the UI is half Chinese, half English even though the `.po` does contain the
 * translation. This assertion blocks that "looks translated, never took effect" class outright.
 */
void I18nConsistencyTest::noFuzzyEntriesInTheCatalog()
{
    const QString poFile = sourceDir() + QStringLiteral("/po/zh_CN/kcm_docker.po");
    QFile file(poFile);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));

    QStringList fuzzyMsgids;
    for (int i = 0; i < lines.size(); ++i) {
        if (lines.at(i).trimmed() != QLatin1String("#, fuzzy")) {
            continue;
        }
        // Find this entry's msgid so the failure names the culprit directly
        for (int j = i + 1; j < lines.size() && j < i + 8; ++j) {
            if (lines.at(j).startsWith(QLatin1String("msgid "))) {
                fuzzyMsgids.append(lines.at(j).mid(6));
                break;
            }
        }
    }
    QVERIFY2(fuzzyMsgids.isEmpty(),
             qPrintable(QStringLiteral("%1 fuzzy entries (they are NOT compiled into the .mo, so i18n() "
                                       "silently falls back to English): %2")
                            .arg(fuzzyMsgids.size())
                            .arg(fuzzyMsgids.join(QStringLiteral(", ")))));
}


QTEST_GUILESS_MAIN(I18nConsistencyTest)
#include "tst_i18n_consistency.moc"