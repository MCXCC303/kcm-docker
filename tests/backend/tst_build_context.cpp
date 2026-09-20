/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
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
 * Build-context packing (ARCH_V5_V8 §5.2).
 *
 * Failures here surface as a mysteriously failing build, so these cases cover: content really
 * reaches the tar, the common `.dockerignore` subset, **escaping symlinks must be blocked**,
 * and no temp file left behind on a limit violation or failure.
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
    /*! Read tar entry name → content (empty for directory entries). */
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
    // Recurse: KArchiveDirectory::entries() lists only the first level
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

    // Deleted after upload: the file is really gone
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
    // Files matched by a glob rule stay out of the context
    QVERIFY2(!names.contains(QStringLiteral("secret.key")), "an ignored file must not be packed");
    // Directory rule: the whole subtree is excluded
    QVERIFY2(!names.contains(QStringLiteral("logs/app.log")), "an ignored directory must not be packed");
    QVERIFY2(!names.contains(QStringLiteral("node_modules/dep/index.js")), "an ignored directory must not be packed");
    // A negation rule does **not** rescue a file under an excluded parent (same as Docker)
    QVERIFY2(!names.contains(QStringLiteral("logs/keep.log")),
             "a file inside an excluded directory cannot be re-included (same as Docker)");

    // A negation rule only applies when the parent directory is not excluded
    writeFile(context, QStringLiteral("ignored.tmp"), QByteArrayLiteral("tmp\n"));
    writeFile(context, QStringLiteral(".dockerignore"), QByteArrayLiteral("*.tmp\n!keep.tmp\n"));
    writeFile(context, QStringLiteral("keep.tmp"), QByteArrayLiteral("keep\n"));
    const BuildContextResult negated = packBuildContext(options, workspace.filePath(QStringLiteral("tmp2")));
    QVERIFY2(negated.ok, qPrintable(negated.errorKey));
    const QMap<QString, QByteArray> negatedEntries = readArchive(negated.archivePath);
    QVERIFY2(!negatedEntries.contains(QStringLiteral("ignored.tmp")), "a matching file stays ignored");
    QVERIFY2(negatedEntries.contains(QStringLiteral("keep.tmp")), "the negated file must be packed");
    removeArchive(negated.archivePath);
    // The ignore file itself is not part of the context
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

    // Points outside the context: must be skipped (path-traversal guard) and reported
    QVERIFY(QFile::link(workspace.filePath(QStringLiteral("outside.txt")),
                        context + QStringLiteral("/escape.txt")));
    // Points inside the context: may enter the tar
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

    // Directory does not exist
    BuildContextOptions missing;
    missing.directory = workspace.filePath(QStringLiteral("nope"));
    QCOMPARE(packBuildContext(missing, temp).errorKey, QStringLiteral("contextMissing"));

    // No Dockerfile in the directory: fail locally with a clearer hint than the engine's 500
    BuildContextOptions noDockerfile;
    noDockerfile.directory = context;
    const BuildContextResult noFile = packBuildContext(noDockerfile, temp);
    QCOMPARE(noFile.errorKey, QStringLiteral("dockerfileMissing"));
    QVERIFY2(noFile.archivePath.isEmpty(), "a failed pack must not leave an archive behind");

    writeFile(context, QStringLiteral("Dockerfile"), QByteArrayLiteral("FROM alpine:3.19\n"));
    writeFile(context, QStringLiteral("big.bin"), QByteArray(4096, 'b'));

    // Size limit: fails and **leaves no temp file**
    BuildContextOptions tooLarge;
    tooLarge.directory = context;
    tooLarge.maxBytes = 1024;
    const BuildContextResult large = packBuildContext(tooLarge, temp);
    QCOMPARE(large.errorKey, QStringLiteral("contextTooLarge"));
    QVERIFY(large.archivePath.isEmpty());
    const QStringList leftovers = QDir(temp).entryList({QStringLiteral("*.tar")}, QDir::Files);
    QVERIFY2(leftovers.isEmpty(), qPrintable(leftovers.join(QLatin1Char(','))));

    // File count limit
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
    // No Dockerfile on disk, but the UI can paste an inline one
    options.inlineDockerfile = QStringLiteral("FROM alpine:3.19\nRUN echo hello\n");
    const BuildContextResult result = packBuildContext(options, workspace.filePath(QStringLiteral("tmp")));
    QVERIFY2(result.ok, qPrintable(result.errorKey));

    const QMap<QString, QByteArray> entries = readArchive(result.archivePath);
    QCOMPARE(entries.value(QStringLiteral("Dockerfile")), QByteArrayLiteral("FROM alpine:3.19\nRUN echo hello\n"));
}

QTEST_MAIN(BuildContextTest)

#include "tst_build_context.moc"
