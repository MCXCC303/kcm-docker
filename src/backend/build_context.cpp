/*
    SPDX-FileCopyrightText: 2026 kontainer developers
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
/*! `.dockerignore` 的一条规则（只实现规范里最常用的子集）。 */
struct IgnoreRule {
    QRegularExpression pattern;
    bool negated = false;
};

/*!
 * 把 `.dockerignore` 的一行编译成正则。
 *
 * 支持的写法：`#` 注释与空行、`!` 取反、`*` 匹配任意字符（不跨 `/`）、`?` 匹配单字符、
 * 末尾 `/` 只匹配目录、开头 `/` 锚定到上下文根。**不做**字符类与 `**`（登记为偏离）。
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

    // 目录规则（`logs/`）：目录本身与它下面的**整棵子树**都算命中
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
            ignored = !rule.negated; // 后面的规则覆盖前面的
        }
    }
    return ignored;
}

/*! 读 `.dockerignore`（不存在就是没有规则）。 */
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

/*! 目标路径是否落在上下文目录内（防符号链接穿越）。 */
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
        // 引擎在缺 Dockerfile 时回 500 "Cannot locate specified Dockerfile"，
        // 本地先给更清楚的提示（§5.2 前置校验）
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
    QStringList symlinkTargets; // 相对路径列表（与 tar 里的链接分开收集，最后统一写）
    QStringList symlinkPaths;
    qint64 totalBytes = 0;
    int fileCount = 0;
    QString failureKey;
    QString failureDetail;

    {
    // 只写文件条目（空目录不会进 tar，Docker 侧无所谓）：addLocalDirectory 是递归的，
    // 用它会把整棵树重复写两遍，因此这里按条目自己加
    KTar tar(archivePath, QStringLiteral("application/x-tar"));
    if (!tar.open(QIODevice::WriteOnly)) {
        result.errorKey = QStringLiteral("archiveFailed");
        return result;
    }

    /* 自己递归（而不是 QDirIterator）：目录被 .dockerignore 排除时要能**整棵子树**跳过 */
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
                // 符号链接：指向上下文之外的**跳过**（防目录穿越），指向里面的按链接写进 tar
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
                continue; // 忽略规则本身不必进上下文
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
            // KF6 的 addLocalFile 只有两个参数：第二个就是 tar 里的完整路径
            tar.addLocalFile(info.absoluteFilePath(), relative);
        }
    };
    walk(QString());

    if (failureKey.isEmpty() && inlineDockerfile) {
        // 内联内容：先落到临时文件再进 tar（KTar 没有"直接写一段内容"的接口）
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
    } // KTar 在这里析构：必须等它彻底放手之后再删临时文件，否则它会把文件又写出来

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
