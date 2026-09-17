/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/build_context.h"

#include <KArchiveDirectory>
#include <KArchiveEntry>
#include <KArchiveFile>
#include <KTar>

#include <QDir>
#include <functional>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace Kontainer;

/*!
 * 构建上下文打包（ARCH_V5_V8 §5.2）。
 *
 * 这一层的错误都会变成"构建莫名其妙失败"，因此用例覆盖：内容真的进了 tar、
 * `.dockerignore` 的常用子集、**越界符号链接必须被挡掉**、上限与失败时不留临时文件。
 */
class BuildContextTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void packsTheDirectoryIntoATar();
    void honoursTheDockerignoreSubset();
    void skipsSymlinksThatEscapeTheContext();
    void rejectsMissingDockerfileAndOversizedContexts();
    void writesAnInlineDockerfile();

private:
    static QString writeFile(const QString &directory, const QString &relative, const QByteArray &content);
    /*! 读出 tar 里的条目名 → 内容（目录条目值为空）。 */
    static QMap<QString, QByteArray> readArchive(const QString &path);
};

QString BuildContextTest::writeFile(const QString &directory, const QString &relative, const QByteArray &content)
{
    const QString path = QDir(directory).filePath(relative);
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return {};
    }
    file.write(content);
    file.close();
    return path;
}

QMap<QString, QByteArray> BuildContextTest::readArchive(const QString &path)
{
    QMap<QString, QByteArray> entries;
    KTar tar(path);
    if (!tar.open(QIODevice::ReadOnly)) {
        return entries;
    }
    const KArchiveDirectory *root = tar.directory();
    if (!root) {
        return entries;
    }
    // 递归：KArchiveDirectory::entries() 只给第一层
    std::function<void(const KArchiveDirectory *, const QString &)> walk = [&](const KArchiveDirectory *directory,
                                                                              const QString &prefix) {
        const QStringList names = directory->entries();
        for (const QString &name : names) {
            const KArchiveEntry *entry = directory->entry(name);
            if (!entry) {
                continue;
            }
            const QString fullPath = prefix.isEmpty() ? name : prefix + QLatin1Char('/') + name;
            if (entry->isDirectory()) {
                entries.insert(fullPath + QLatin1Char('/'), QByteArray());
                walk(static_cast<const KArchiveDirectory *>(entry), fullPath);
            } else if (entry->isFile()) {
                entries.insert(fullPath, static_cast<const KArchiveFile *>(entry)->data());
            } else {
                entries.insert(fullPath, QByteArray());
            }
        }
    };
    walk(root, QString());
    tar.close();
    return entries;
}

void BuildContextTest::packsTheDirectoryIntoATar()
{
    QTemporaryDir workspace;
    QVERIFY(workspace.isValid());
    const QString context = workspace.filePath(QStringLiteral("context"));
    QVERIFY(QDir().mkpath(context));
    writeFile(context, QStringLiteral("Dockerfile"), QByteArrayLiteral("FROM alpine:3.19\n"));
    writeFile(context, QStringLiteral("app/main.js"), QByteArrayLiteral("console.log(1)\n"));
    writeFile(context, QStringLiteral("app/nested/data.txt"), QByteArrayLiteral("nested\n"));

    BuildContextOptions options;
    options.directory = context;
    const BuildContextResult result = packBuildContext(options, workspace.filePath(QStringLiteral("tmp")));
    QVERIFY2(result.ok, qPrintable(result.errorKey + QLatin1Char(' ') + result.errorDetail));
    QCOMPARE(result.fileCount, 3);
    QVERIFY(result.bytes > 0);

    const QMap<QString, QByteArray> entries = readArchive(result.archivePath);
    QVERIFY2(entries.contains(QStringLiteral("Dockerfile")), qPrintable(entries.keys().join(QLatin1Char(','))));
    QCOMPARE(entries.value(QStringLiteral("Dockerfile")), QByteArrayLiteral("FROM alpine:3.19\n"));
    QCOMPARE(entries.value(QStringLiteral("app/main.js")), QByteArrayLiteral("console.log(1)\n"));
    QCOMPARE(entries.value(QStringLiteral("app/nested/data.txt")), QByteArrayLiteral("nested\n"));

    // 上传完删掉：文件真的没了
    const QString archivePath = result.archivePath;
    removeArchive(archivePath);
    QVERIFY(!QFile::exists(archivePath));
}

void BuildContextTest::honoursTheDockerignoreSubset()
{
    QTemporaryDir workspace;
    QVERIFY(workspace.isValid());
    const QString context = workspace.filePath(QStringLiteral("context"));
    QVERIFY(QDir().mkpath(context));
    writeFile(context, QStringLiteral("Dockerfile"), QByteArrayLiteral("FROM alpine:3.19\n"));
    writeFile(context, QStringLiteral("keep.txt"), QByteArrayLiteral("keep\n"));
    writeFile(context, QStringLiteral("secret.key"), QByteArrayLiteral("secret\n"));
    writeFile(context, QStringLiteral("logs/app.log"), QByteArrayLiteral("log\n"));
    writeFile(context, QStringLiteral("logs/keep.log"), QByteArrayLiteral("log\n"));
    writeFile(context, QStringLiteral("node_modules/dep/index.js"), QByteArrayLiteral("dep\n"));
    writeFile(context,
              QStringLiteral(".dockerignore"),
              QByteArrayLiteral("# 注释与空行\n\n"
                                "*.key\n"
                                "logs/\n"
                                "node_modules\n"
                                "!keep.txt\n"));

    BuildContextOptions options;
    options.directory = context;
    const BuildContextResult result = packBuildContext(options, workspace.filePath(QStringLiteral("tmp")));
    QVERIFY2(result.ok, qPrintable(result.errorKey));

    const QMap<QString, QByteArray> entries = readArchive(result.archivePath);
    const QStringList names = entries.keys();
    QVERIFY2(names.contains(QStringLiteral("Dockerfile")), qPrintable(names.join(QLatin1Char(','))));
    QVERIFY2(names.contains(QStringLiteral("keep.txt")), qPrintable(names.join(QLatin1Char(','))));
    // 通配规则命中的文件不进上下文
    QVERIFY2(!names.contains(QStringLiteral("secret.key")), "an ignored file must not be packed");
    // 目录规则：整棵子树被排除
    QVERIFY2(!names.contains(QStringLiteral("logs/app.log")), "an ignored directory must not be packed");
    QVERIFY2(!names.contains(QStringLiteral("node_modules/dep/index.js")), "an ignored directory must not be packed");
    // 父目录被排除时子文件**不会**被取反规则救回来（与 Docker 的语义一致）
    QVERIFY2(!names.contains(QStringLiteral("logs/keep.log")),
             "a file inside an excluded directory cannot be re-included (same as Docker)");

    // 取反规则只在"父目录没被排除"时生效
    writeFile(context, QStringLiteral("ignored.tmp"), QByteArrayLiteral("tmp\n"));
    writeFile(context, QStringLiteral(".dockerignore"), QByteArrayLiteral("*.tmp\n!keep.tmp\n"));
    writeFile(context, QStringLiteral("keep.tmp"), QByteArrayLiteral("keep\n"));
    const BuildContextResult negated = packBuildContext(options, workspace.filePath(QStringLiteral("tmp2")));
    QVERIFY2(negated.ok, qPrintable(negated.errorKey));
    const QMap<QString, QByteArray> negatedEntries = readArchive(negated.archivePath);
    QVERIFY2(!negatedEntries.contains(QStringLiteral("ignored.tmp")), "a matching file stays ignored");
    QVERIFY2(negatedEntries.contains(QStringLiteral("keep.tmp")), "the negated file must be packed");
    removeArchive(negated.archivePath);
    // 忽略规则本身不占上下文
    QVERIFY2(!names.contains(QStringLiteral(".dockerignore")), "the ignore file itself is not needed");
}

void BuildContextTest::skipsSymlinksThatEscapeTheContext()
{
    QTemporaryDir workspace;
    QVERIFY(workspace.isValid());
    const QString context = workspace.filePath(QStringLiteral("context"));
    QVERIFY(QDir().mkpath(context));
    writeFile(context, QStringLiteral("Dockerfile"), QByteArrayLiteral("FROM alpine:3.19\n"));
    writeFile(context, QStringLiteral("inside.txt"), QByteArrayLiteral("inside\n"));
    writeFile(workspace.path(), QStringLiteral("outside.txt"), QByteArrayLiteral("outside\n"));

    // 指向上下文之外：必须跳过（防目录穿越），并且如实报告
    QVERIFY(QFile::link(workspace.filePath(QStringLiteral("outside.txt")),
                        context + QStringLiteral("/escape.txt")));
    // 指向上下文之内：可以进 tar
    QVERIFY(QFile::link(context + QStringLiteral("/inside.txt"), context + QStringLiteral("/inside-link.txt")));

    BuildContextOptions options;
    options.directory = context;
    const BuildContextResult result = packBuildContext(options, workspace.filePath(QStringLiteral("tmp")));
    QVERIFY2(result.ok, qPrintable(result.errorKey));
    QVERIFY2(result.skipped.contains(QStringLiteral("escape.txt")), "the escaping symlink must be reported");

    const QMap<QString, QByteArray> entries = readArchive(result.archivePath);
    QVERIFY2(!entries.contains(QStringLiteral("escape.txt")), "a symlink pointing outside must never be packed");
    QVERIFY2(!entries.contains(QStringLiteral("outside.txt")), "the target must not be packed either");
    QVERIFY2(entries.contains(QStringLiteral("inside-link.txt")), "a symlink inside the context is fine");
}

void BuildContextTest::rejectsMissingDockerfileAndOversizedContexts()
{
    QTemporaryDir workspace;
    QVERIFY(workspace.isValid());
    const QString context = workspace.filePath(QStringLiteral("context"));
    QVERIFY(QDir().mkpath(context));
    const QString temp = workspace.filePath(QStringLiteral("tmp"));

    // 目录不存在
    BuildContextOptions missing;
    missing.directory = workspace.filePath(QStringLiteral("nope"));
    QCOMPARE(packBuildContext(missing, temp).errorKey, QStringLiteral("contextMissing"));

    // 目录里没有 Dockerfile：本地就给出比引擎 500 更清楚的提示
    BuildContextOptions noDockerfile;
    noDockerfile.directory = context;
    const BuildContextResult noFile = packBuildContext(noDockerfile, temp);
    QCOMPARE(noFile.errorKey, QStringLiteral("dockerfileMissing"));
    QVERIFY2(noFile.archivePath.isEmpty(), "a failed pack must not leave an archive behind");

    writeFile(context, QStringLiteral("Dockerfile"), QByteArrayLiteral("FROM alpine:3.19\n"));
    writeFile(context, QStringLiteral("big.bin"), QByteArray(4096, 'b'));

    // 大小上限：失败并且**不留临时文件**
    BuildContextOptions tooLarge;
    tooLarge.directory = context;
    tooLarge.maxBytes = 1024;
    const BuildContextResult large = packBuildContext(tooLarge, temp);
    QCOMPARE(large.errorKey, QStringLiteral("contextTooLarge"));
    QVERIFY(large.archivePath.isEmpty());
    const QStringList leftovers = QDir(temp).entryList({QStringLiteral("*.tar")}, QDir::Files);
    QVERIFY2(leftovers.isEmpty(), qPrintable(leftovers.join(QLatin1Char(','))));

    // 文件数上限
    BuildContextOptions tooMany;
    tooMany.directory = context;
    tooMany.maxFiles = 1;
    QCOMPARE(packBuildContext(tooMany, temp).errorKey, QStringLiteral("tooManyFiles"));
}

void BuildContextTest::writesAnInlineDockerfile()
{
    QTemporaryDir workspace;
    QVERIFY(workspace.isValid());
    const QString context = workspace.filePath(QStringLiteral("context"));
    QVERIFY(QDir().mkpath(context));
    writeFile(context, QStringLiteral("app.txt"), QByteArrayLiteral("x\n"));

    BuildContextOptions options;
    options.directory = context;
    // 目录里没有 Dockerfile，但界面可以贴一份内联内容
    options.inlineDockerfile = QStringLiteral("FROM alpine:3.19\nRUN echo hello\n");
    const BuildContextResult result = packBuildContext(options, workspace.filePath(QStringLiteral("tmp")));
    QVERIFY2(result.ok, qPrintable(result.errorKey));

    const QMap<QString, QByteArray> entries = readArchive(result.archivePath);
    QCOMPARE(entries.value(QStringLiteral("Dockerfile")), QByteArrayLiteral("FROM alpine:3.19\nRUN echo hello\n"));
}

QTEST_MAIN(BuildContextTest)

#include "tst_build_context.moc"
