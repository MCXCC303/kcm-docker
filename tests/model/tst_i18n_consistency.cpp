/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "i18n.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
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
    void noUnwrappedUiStrings();
};

namespace
{
QString sourceDir()
{
    return QStringLiteral(KONTAINER_SOURCE_DIR);
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
        const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
        for (int index = 0; index < lines.size(); ++index) {
            const QString &line = lines.at(index);
            if (line.contains(QLatin1String("i18n-lint: allow"))) {
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

QTEST_GUILESS_MAIN(I18nConsistencyTest)
#include "tst_i18n_consistency.moc"
