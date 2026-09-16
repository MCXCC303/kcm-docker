/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "i18n.h"

#include <QDir>
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

QTEST_GUILESS_MAIN(I18nConsistencyTest)

#include "tst_i18n_consistency.moc"
