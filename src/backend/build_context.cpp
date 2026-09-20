/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/build_context.h"

#include "logging.h"

#include <KArchive>
#include <KTar>

#include <QDir>
#include <functional>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryFile>

namespace Kontainer
{

namespace
{
/*! One `.dockerignore` rule (only the most common subset of the spec is implemented). */
struct IgnoreRule {
    QRegularExpression pattern;
    bool negated = false;
};

/*!
 * Compile one `.dockerignore` line into a regex.
 *
 * Supported: `#` comments and blank lines, `!` negation, `*` (any character, not across `/`),
 * `?` (single character), trailing `/` (directories only), leading `/` (anchored to the context
 * root). Character classes and `**` are **not** implemented (logged as a deviation).
 */
bool compileIgnoreRule(const QString &line, IgnoreRule *rule)
{
    QString text = line.trimmed();
    if (text.isEmpty() || text.startsWith(QLatin1Char('#'))) {
        return false;
    }
    rule->negated = text.startsWith(QLatin1Char('!'));
    if (rule->negated) {
        text = text.mid(1);
    }
    const bool anchored = text.startsWith(QLatin1Char('/'));
    if (anchored) {
        text = text.mid(1);
    }
    bool directoryOnly = false;
    if (text.endsWith(QLatin1Char('/'))) {
        directoryOnly = true;
        text.chop(1);
    }

    const QString prefix = anchored ? QStringLiteral("^") : QStringLiteral("(^|.*/)");
    QString body;
    for (const QChar &character : std::as_const(text)) {
        if (character == QLatin1Char('*')) {
            body += QStringLiteral("[^/]*");
        } else if (character == QLatin1Char('?')) {
            body += QStringLiteral("[^/]");
        } else {
            body += QRegularExpression::escape(QString(character));
        }
    }

    // Directory rule (`logs/`): the directory itself and its **whole subtree** match
    const QString pattern = directoryOnly ? prefix + body + QStringLiteral("(/.*)?$")
                                          : prefix + body + QLatin1Char('$');
    rule->pattern = QRegularExpression(pattern);
    return rule->pattern.isValid();
}

bool isIgnored(const QString &relativePath, bool isDirectory, const QList<IgnoreRule> &rules)
{
    bool ignored = false;
    for (const IgnoreRule &rule : rules) {
        Q_UNUSED(isDirectory);
        if (rule.pattern.match(relativePath).hasMatch()) {
            ignored = !rule.negated; // later rules override earlier ones
        }
    }
    return ignored;
}

/*! Read `.dockerignore` (a missing file means no rules). */
QList<IgnoreRule> readIgnoreRules(const QString &contextDirectory)
{
    QList<IgnoreRule> rules;
    QFile file(QDir(contextDirectory).filePath(QStringLiteral(".dockerignore")));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return rules;
    }
    while (!file.atEnd()) {
        IgnoreRule rule;
        if (compileIgnoreRule(QString::fromUtf8(file.readLine()), &rule)) {
            rules.append(rule);
        }
    }
    return rules;
}

/*! Whether the target path stays inside the context directory (anti symlink traversal). */
bool staysInside(const QString &canonicalRoot, const QString &candidate)
{
    const QString canonical = QFileInfo(candidate).canonicalFilePath();
    if (canonical.isEmpty()) {
        return false;
    }
    return canonical == canonicalRoot || canonical.startsWith(canonicalRoot + QLatin1Char('/'));
}
} // namespace

BuildContextResult packBuildContext(const BuildContextOptions &options, const QString &tempDirectory)
{
    BuildContextResult result;

    const QFileInfo directoryInfo(options.directory);
    if (!directoryInfo.exists()) {
        result.errorKey = QStringLiteral("contextMissing");
        return result;
    }
    if (!directoryInfo.isDir()) {
        result.errorKey = QStringLiteral("notADirectory");
        return result;
    }
    const QString root = directoryInfo.canonicalFilePath();

    const bool inlineDockerfile = !options.inlineDockerfile.isEmpty();
    if (!inlineDockerfile && !QFileInfo::exists(QDir(root).filePath(options.dockerfile))) {
        // The engine answers 500 "Cannot locate specified Dockerfile" when it is missing;
        // check locally first for a clearer message (§5.2 pre-check)
        result.errorKey = QStringLiteral("dockerfileMissing");
        result.errorDetail = options.dockerfile;
        return result;
    }

    const QString base = tempDirectory.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                                                 : tempDirectory;
    QDir().mkpath(base);
    QTemporaryFile tempFile(QDir(base).filePath(QStringLiteral("kontainer-build-XXXXXX.tar")));
    tempFile.setAutoRemove(false);
    if (!tempFile.open()) {
        result.errorKey = QStringLiteral("archiveFailed");
        result.errorDetail = tempFile.errorString();
        return result;
    }
    const QString archivePath = tempFile.fileName();
    tempFile.close();

    const QList<IgnoreRule> rules = readIgnoreRules(root);
    QStringList symlinkTargets; // relative paths (collected apart from tar links, written last)
    QStringList symlinkPaths;
    qint64 totalBytes = 0;
    int fileCount = 0;
    QString failureKey;
    QString failureDetail;

    {
    // Only file entries are written (empty dirs never reach the tar, which is fine for Docker):
    // addLocalDirectory is recursive and would write the tree twice, so entries are added here
    KTar tar(archivePath, QStringLiteral("application/x-tar"));
    if (!tar.open(QIODevice::WriteOnly)) {
        result.errorKey = QStringLiteral("archiveFailed");
        return result;
    }

    /* Recurse manually (not QDirIterator): an ignored directory must skip its **whole subtree** */
    std::function<void(const QString &)> walk = [&](const QString &relativeDirectory) {
        if (!failureKey.isEmpty()) {
            return;
        }
        const QString absoluteDirectory = relativeDirectory.isEmpty() ? root : QDir(root).filePath(relativeDirectory);
        const QFileInfoList entries = QDir(absoluteDirectory)
                                          .entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden
                                                             | QDir::System,
                                                         QDir::Name | QDir::DirsFirst);
        for (const QFileInfo &info : entries) {
            if (!failureKey.isEmpty()) {
                return;
            }
            const QString relative = relativeDirectory.isEmpty()
                ? info.fileName()
                : relativeDirectory + QLatin1Char('/') + info.fileName();
            const bool isDirectory = info.isDir() && !info.isSymLink();

            if (isIgnored(relative, isDirectory, rules)) {
                continue;
            }
            if (info.isSymLink()) {
                // Symlinks escaping the context are **skipped**; inside ones are written as tar links
                if (!staysInside(root, info.absoluteFilePath())) {
                    qCWarning(kontainerBackend) << "skipping symlink that escapes the build context:" << relative;
                    result.skipped.append(relative);
                    continue;
                }
                symlinkPaths.append(relative);
                symlinkTargets.append(info.symLinkTarget());
                continue;
            }
            if (isDirectory) {
                walk(relative);
                continue;
            }
            if (relative == QLatin1String(".dockerignore")) {
                continue; // the ignore file itself need not enter the context
            }

            ++fileCount;
            totalBytes += info.size();
            if (fileCount > options.maxFiles) {
                failureKey = QStringLiteral("tooManyFiles");
                failureDetail = QString::number(options.maxFiles);
                return;
            }
            if (totalBytes > options.maxBytes) {
                failureKey = QStringLiteral("contextTooLarge");
                failureDetail = QString::number(options.maxBytes);
                return;
            }
            // KF6 addLocalFile takes only two arguments: the second is the full path inside the tar
            tar.addLocalFile(info.absoluteFilePath(), relative);
        }
    };
    walk(QString());

    if (failureKey.isEmpty() && inlineDockerfile) {
        // Inline content: spill to a temp file before adding (KTar has no "write this content" API)
        QTemporaryFile inlineFile(QDir(base).filePath(QStringLiteral("kontainer-inline-XXXXXX")));
        inlineFile.setAutoRemove(true);
        if (!inlineFile.open()) {
            failureKey = QStringLiteral("archiveFailed");
            failureDetail = inlineFile.errorString();
        } else {
            inlineFile.write(options.inlineDockerfile.toUtf8());
            inlineFile.flush();
            tar.addLocalFile(inlineFile.fileName(), QStringLiteral("Dockerfile"));
        }
    }

    if (failureKey.isEmpty()) {
        for (int i = 0; i < symlinkPaths.size(); ++i) {
            tar.writeSymLink(symlinkPaths.at(i), symlinkTargets.at(i));
        }
    }

    if (failureKey.isEmpty() && !tar.close()) {
        failureKey = QStringLiteral("archiveFailed");
    }
    } // KTar destructs here: delete the temp file only after it fully lets go, or it writes it again

    if (!failureKey.isEmpty()) {
        removeArchive(archivePath);
        result.errorKey = failureKey;
        result.errorDetail = failureDetail;
        return result;
    }

    result.ok = true;
    result.archivePath = archivePath;
    result.bytes = QFileInfo(archivePath).size();
    result.fileCount = fileCount;
    return result;
}

void removeArchive(const QString &archivePath)
{
    if (archivePath.isEmpty()) {
        return;
    }
    QFile::remove(archivePath);
}

} // namespace Kontainer
