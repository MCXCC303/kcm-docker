/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <QtTest>

namespace
{
QString sourceDir()
{
    return QStringLiteral(KONTAINER_SOURCE_DIR);
}

/*! 递归收集某个目录下的文件内容（`relativePath -> content`）。 */
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
 * 命中给定模式的行（跳过注释行：注释里提到某个 token 名字不算真的在使用它）。
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
 * 源码约定测试（ARCH_V3 §2.1 的验收条件）。
 *
 * 这些约定靠代码评审很容易漏：一次复制粘贴就能让「状态配色只有一处实现」
 * 或「只读边界」悄悄失效，而它们在界面上不会立刻表现出来。
 * 因此把它们写成可执行的断言。
 */
class SourceConventionsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void copyActionHasSingleImplementation();
    void statusColorsStayInPalettes();
    void productionCodeStaysReadOnly();
};

/*!
 * 复制动作只允许有一个实现（CopyButton）：以前这段逻辑在两个详情页里手写了 6 遍。
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
 * 状态语义色只允许出现在 StatusPalette 里（ARCH_V2 §12 / ARCH_V3 §2.1）。
 * 数据可视化使用 ChartPalette 的固定取色，两者不得混用。
 */
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
 * 只读边界（ARCH_V1 §24 / README「只读安全边界」）：
 * 生产代码不得出现写请求动词、不得调用 docker CLI、不得引入提权机制。
 *
 * 三期仍然只读；这条断言保证「打开写操作」必须是一次显式的、被审阅的改动，
 * 而不是某次顺手加上的 POST。
 */
void SourceConventionsTest::productionCodeStaysReadOnly()
{
    const QMap<QString, QString> sources = collectFiles(sourceDir() + QStringLiteral("/src"), {QStringLiteral("*.cpp"), QStringLiteral("*.h"), QStringLiteral("*.qml")});
    QVERIFY2(!sources.isEmpty(), "no production sources found");

    // 写请求动词（作为字符串字面量出现时才是真的在发请求）
    const QRegularExpression writeVerb(QStringLiteral("\"(POST|PUT|PATCH|DELETE)\""));
    // 外部进程 / 提权
    const QRegularExpression forbidden(QStringLiteral("(QProcess|KAuth|KAuth\\b|kauth)"));

    QStringList offenders;
    for (auto it = sources.constBegin(); it != sources.constEnd(); ++it) {
        for (const QString &hit : linesMatching(it.value(), writeVerb)) {
            offenders.append(QStringLiteral("%1 → %2").arg(it.key(), hit));
        }
        for (const QString &hit : linesMatching(it.value(), forbidden)) {
            offenders.append(QStringLiteral("%1 → %2").arg(it.key(), hit));
        }
    }
    QVERIFY2(offenders.isEmpty(),
             qPrintable(QStringLiteral("Kontainer is read-only by design; mutation needs an explicit design change:\n%1").arg(offenders.join(QLatin1Char('\n')))));
}

QTEST_GUILESS_MAIN(SourceConventionsTest)

#include "tst_source_conventions.moc"
