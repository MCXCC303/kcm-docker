/*
    SPDX-FileCopyrightText: 2026 kontainer developers
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
 * i18n 一致性测试。
 *
 * 防止踩过的坑再次出现：KCM 的翻译域必须等于插件 id（KDE 惯例），
 * 元数据里的 TranslationDomain、po/ 目录名与 kTranslationDomain 必须一致，
 * 否则界面会静默退回英文（QML 与 C++ 可能一半中文一半英文）。
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
    // 译文安装为 share/locale/<lang>/LC_MESSAGES/<domain>.mo，
    // KCM 的 QML 侧使用插件 id 作为域，因此两者必须相同。
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

    // 逐条检查：msgid 非空但 msgstr（或 msgstr[0]）为空的条目即为未翻译
    // 注意：跳过文件头部那条 msgid "" 的元数据条目
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
 * ARCH_V3 §2.6：界面里的用户可见文本不得直接写成字符串字面量。
 *
 * 只检查**明确承载用户可见文本**的属性（text / title / 无障碍名称 / 工具提示 …），
 * 不检查 objectName、icon.name、source、font.family 这些非文本属性，
 * 因此几乎不会误报。
 *
 * 确实需要字面量时（例如纯符号占位），在该行加注释 `i18n-lint: allow <理由>`。
 * 这说明该例外是被审阅过的，而不是漏网。
 */
namespace
{

/*!
 * 把注释替换成空格（保持行号与字符串内容不变）。
 *
 * 为什么需要它：lint 是逐行正则，而文档注释里经常出现示例代码
 * （例如组件用法里写 `text: "80/tcp"`）。那不是真的界面文案，
 * 但如果不过滤注释，它会被当成违规——断言一旦开始误报，
 * 下一个人就会选择把它关掉，而不是修它。
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
 * 模板必须覆盖源码里的全部待译字符串。
 *
 * 为什么要有这条：五期一次就漏了 90 多条（新增界面文案没进 `po/`），
 * 而"漏了"在界面上表现为英文与中文混排——只有真的有人切到中文才看得见。
 * 这里直接跑一次 xgettext（与 `po/README.md` 里给译者的命令一致），
 * 只比较 **msgctxt + msgid + msgid_plural** 集合，不比行号引用
 * （引用行每次改代码都会变，比它只会制造噪音）。
 */
namespace
{

/*!
 * 把 po/pot 解析成 `msgctxt\x1fmsgid\x1fmsgid_plural` 字符串集合。
 *
 * 只做这一个测试需要的事：按空行切块 → 取三个字段 → 去掉引号与续行。
 * 不做通用解析（那需要一个完整的 gettext 实现，而这里的输入是我们自己的文件）。
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
            continue; // 已废弃的条目
        }
        const QStringList block = raw.split(QLatin1Char('\n'));
        const QString id = field(block, QStringLiteral("msgid"));
        if (id.isEmpty()) {
            continue; // 头部元数据条目
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

    // 与 po/README.md 里写给译者的命令保持一致（否则两边会得出不同的集合）
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
 * 译文真的能被 ki18n 读出来吗？
 *
 * 前三个用例查的是"文件里有没有"，这个用例查的是"运行时会显示什么"：
 * 域、语言、安装目录、.mo 内容任何一环错了，界面都会静默退回英文，
 * 而那种失败在 CI 里看不出来（除非有人真的切到中文看一眼）。
 */
void I18nConsistencyTest::translationsLoadAtRuntime()
{
    const QString moFile = QStringLiteral(KCM_DOCKER_BUILD_DIR) + QStringLiteral("/locale/zh_CN/LC_MESSAGES/kcm_docker.mo");
    if (!QFile::exists(moFile)) {
        QSKIP("the compiled .mo does not exist yet; build the kcm_docker target first");
    }

    // 语言显式钉住：测试不该随开发者机器的 LANG 变化
    // （XDG_DATA_DIRS 与 LANGUAGE 由 tests/CMakeLists.txt 注入进程环境，
    //  QStandardPaths 在进程启动后只初始化一次，所以不能在测试体里 qputenv）
    KLocalizedString::setLanguages({QStringLiteral("zh_CN")});
    setupTranslationDomain();

    // 抽三条覆盖不同来源（QML/C++、普通条目、复数条目）
    QCOMPARE(i18n("Save"), QStringLiteral("保存"));
    QCOMPARE(i18n("Unlock to edit"), QStringLiteral("解锁以编辑"));
    QCOMPARE(i18ncp("@info image layer count", "Layers (%1)", "Layers (%1)", 3), QStringLiteral("层（3）"));

    // 详情页的"关联容器/网络成员"用 Presentation 取状态文案：必须和容器列表同一份译文
    Presentation presentation;
    QCOMPARE(presentation.stateText(QStringLiteral("running")), QStringLiteral("运行中"));
    QCOMPARE(presentation.stateText(QStringLiteral("paused")), QStringLiteral("已暂停"));

    // 没有译文的字符串必须原样返回：返回空串会让界面出现空白按钮
    const char *untranslated = "this string is intentionally not translated";
    QCOMPARE(i18n(untranslated), QString::fromLatin1(untranslated));
}

void I18nConsistencyTest::noUnwrappedUiStrings()
{
    // 用普通字符串而不是原始字符串：正则本身以 )" 结尾，原始字符串容易踩到分隔符问题
    const QRegularExpression textProperty(
        // 允许带限定前缀（例如 Kirigami.FormData.label、Accessible.name、QQC2.ToolTip.text），
        // 但叶子名必须是明确承载用户可见文本的属性——这样 icon.name / objectName 不会误报。
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
        // 两份内容：raw 用于识别「豁免标记」（标记本身就在注释里，剥掉注释就看不见了），
        // stripped 用于匹配真正的代码属性赋值（避免文档注释里的示例代码误报）
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
 * 开发时用 `QT_PLUGIN_PATH=<build>/bin` 启动（不带 XDG_DATA_DIRS）也要能翻译。
 *
 * 实测反馈：`QT_PLUGIN_PATH=$PWD/build/bin systemsettings kcm_docker` 在中文环境下显示英文——
 * 因为译文只在 `$XDG_DATA_DIRS/share/locale` 里找，而构建目录与安装前缀都不在其中。
 * `translationLocaleDirs()` 就是为此补的候选目录表：从 `QT_PLUGIN_PATH` 推出
 * `<build>/locale` 与 `<build>/share/locale`，从 Qt 插件目录与可执行文件位置推出
 * `<prefix>/share/locale`。这里用临时目录钉死"推得对、只返回存在的目录"。
 */
void I18nConsistencyTest::developmentPluginPathFindsTheCatalog()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // 模拟构建树：<tmp>/bin（插件）与 <tmp>/locale/zh_CN/LC_MESSAGES/kcm_docker.mo
    QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("bin"))));
    const QString localeDir = dir.filePath(QStringLiteral("locale/zh_CN/LC_MESSAGES"));
    QVERIFY(QDir().mkpath(localeDir));
    QFile catalog(localeDir + QStringLiteral("/kcm_docker.mo"));
    QVERIFY(catalog.open(QIODevice::WriteOnly));
    catalog.write("dummy"); // 只要存在即可：这里测的是"目录推导"，不测翻译内容
    catalog.close();

    const QByteArray previous = qgetenv("QT_PLUGIN_PATH");
    qputenv("QT_PLUGIN_PATH", dir.filePath(QStringLiteral("bin")).toUtf8());
    const QStringList dirs = translationLocaleDirs();
    qputenv("QT_PLUGIN_PATH", previous);

    QVERIFY2(dirs.contains(dir.filePath(QStringLiteral("locale"))),
             qPrintable(QStringLiteral("missing build-tree locale dir, got: %1").arg(dirs.join(QLatin1Char(' ')))));
    // 不存在的目录不能被注册（避免把无效路径塞给 KLocalizedString）
    QVERIFY(!dirs.contains(dir.filePath(QStringLiteral("share/locale"))));

    // 负例方向：没有 .mo 的目录不算数（列表只包含"确实存在"的目录）
    QTemporaryDir empty;
    QVERIFY(empty.isValid());
    qputenv("QT_PLUGIN_PATH", empty.filePath(QStringLiteral("bin")).toUtf8());
    const QStringList emptyDirs = translationLocaleDirs();
    qputenv("QT_PLUGIN_PATH", previous);
    QVERIFY(!emptyDirs.contains(empty.filePath(QStringLiteral("locale"))));
}


/*!
 * 译文不得凭空多出参数占位符。
 *
 * 背景（真实事故）：给 zh_CN 的 .po 批量填译文时，脚本只替换了 `msgstr` 的**第一行**，
 * 旧译文的多行续行留了下来 —— 中文于是变成"两句拼接"，运行时 `%!I(18N_ARGUMENT_MISSING)`
 * 直接显示在悬停提示里（用户截图发现）。判据：`msgstr` 里的 `%N` 必须也出现在
 * 对应的 `msgid` / `msgid_plural` 里。
 */
void I18nConsistencyTest::translationsDoNotInventArguments()
{
    const QString poFile = sourceDir() + QStringLiteral("/po/zh_CN/kcm_docker.po");
    QFile file(poFile);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QString content = QString::fromUtf8(file.readAll());

    // 取出某个字段的完整文本（含多行续行）
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
                value.chop(1); // 去掉结尾引号
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
 * `.po` 里不得残留 `#, fuzzy`。
 *
 * 真实事故（用户两次遇到"这两条字符串没翻译"）：`msgmerge` 会把改动过的条目标成 fuzzy，
 * 而 **fuzzy 条目不会编进 `.mo`** —— 于是 `i18n()` 静默回退英文，界面上半中半英，
 * 而 `.po` 里明明写着译文。这条断言把这类"看起来翻译了、实际没生效"直接挡住。
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
        // 往下找这条的 msgid，报错时能直接看出是哪一条
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