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
    void mutationsHaveSingleChokePoint();
    void restPathsStayInOneHeader();
    void qmlNeverTalksHttp();
    void kioStaysInHostPathService();
    void externalProcessesStayForbidden();
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
 * 写操作的咽喉点（ARCH_V4 §1.5 / §2.2.1）。
 *
 * 三期用一条「生产代码不得出现任何写动词」的断言把只读边界钉死。四期打开写操作时，
 * 这条断言**不是被删掉，而是换成更精确的形状**：写动词与 REST 路径都必须收敛到
 * 唯一一处，界面层完全碰不到传输层。这样「打开写操作」仍然是一次显式、被审阅、
 * 能被测试发现的改动，只是审查对象从「有没有写动词」变成「写动词在哪里」。
 */
void SourceConventionsTest::mutationsHaveSingleChokePoint()
{
    const QMap<QString, QString> sources = collectFiles(sourceDir() + QStringLiteral("/src"), {QStringLiteral("*.cpp"), QStringLiteral("*.h"), QStringLiteral("*.qml")});
    QVERIFY2(!sources.isEmpty(), "no production sources found");

    // 写动词只允许出现在传输层：方法名在那里被写进请求行
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
 * REST 路径只允许出现在 docker_api_paths.h（ARCH_V4 §2.2.1）。
 *
 * 路径散落在多个 .cpp 里时，「这个程序到底会调用哪些端点」就没人能一眼答上来——
 * 对现在有写操作的项目来说，这个问题必须能一眼答上来。
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
 * 界面层不认识传输层（ARCH_V4 §1.5）：QML 里出现 http / 动词 / socket 路径，
 * 说明有请求逻辑漏到了界面里。
 */
void SourceConventionsTest::qmlNeverTalksHttp()
{
    const QMap<QString, QString> qmlFiles = collectFiles(sourceDir() + QStringLiteral("/src/ui"), {QStringLiteral("*.qml")});
    QVERIFY2(!qmlFiles.isEmpty(), "no QML sources found");

    const QRegularExpression transport(QStringLiteral("(https?://|\"GET |\"POST|\"DELETE|unix://|/containers/|/images/)"));
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
 * 打开宿主目录是四期唯一新增的「非 Docker 外部动作」，
 * 因此它必须被限制在一个实现文件里，而不是散落到各个页面。
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
 * 外部进程与提权继续全面禁止（ARCH_V3 §1.3 / ARCH_V4 §1.4）：
 * 不调用 docker CLI，不引入 KAuth，权限模型是「按 socket 实际权限工作」。
 */
void SourceConventionsTest::externalProcessesStayForbidden()
{
    const QMap<QString, QString> sources = collectFiles(sourceDir() + QStringLiteral("/src"), {QStringLiteral("*.cpp"), QStringLiteral("*.h"), QStringLiteral("*.qml")});
    QVERIFY2(!sources.isEmpty(), "no production sources found");

    // 外部进程 / 提权
    const QRegularExpression forbidden(QStringLiteral("(QProcess|KAuth|KAuth\\b|kauth)"));

    QStringList offenders;
    for (auto it = sources.constBegin(); it != sources.constEnd(); ++it) {
        for (const QString &hit : linesMatching(it.value(), forbidden)) {
            offenders.append(QStringLiteral("%1 → %2").arg(it.key(), hit));
        }
    }
    QVERIFY2(offenders.isEmpty(),
             qPrintable(QStringLiteral("Kontainer never shells out and never escalates privileges:\n%1").arg(offenders.join(QLatin1Char('\n')))));
}

QTEST_GUILESS_MAIN(SourceConventionsTest)

#include "tst_source_conventions.moc"
