/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QString>
#include <QStringList>

namespace Kontainer
{

/*!
 * 打包构建上下文（ARCH_V5_V8 §5.2）。
 */
struct BuildContextOptions {
    /*! 上下文目录（用户选的，必须存在）。 */
    QString directory;
    /*! Dockerfile 名称（相对上下文目录；默认 `Dockerfile`）。 */
    QString dockerfile = QStringLiteral("Dockerfile");
    /*! 内联 Dockerfile 内容：非空时把它写进上下文，而不是读目录里的文件（§5.4）。 */
    QString inlineDockerfile;
    /*! 上下文大小上限（字节）。 */
    qint64 maxBytes = 512LL * 1024 * 1024;
    /*! 文件数量上限。 */
    int maxFiles = 20000;
};

/*!
 * 打包结果。
 *
 * 失败时 `errorKey` 是稳定 key（文案在界面侧）：`contextMissing` / `notADirectory` /
 * `dockerfileMissing` / `contextTooLarge` / `tooManyFiles` / `archiveFailed`。
 * 失败时**不会留下临时文件**（部分写出的 tar 会被删掉）。
 */
struct BuildContextResult {
    bool ok = false;
    /*! 打好的 tar（调用方负责上传完删除，用 `removeArchive()`）。 */
    QString archivePath;
    qint64 bytes = 0;
    int fileCount = 0;
    /*! 被跳过的条目（越界的符号链接、被 .dockerignore 排除的不算），给界面提示用。 */
    QStringList skipped;
    QString errorKey;
    QString errorDetail;
};

/*!
 * 把目录打成 tar（不压缩，`application/x-tar`——Docker 接受未压缩上下文）。
 *
 * 安全与边界（§5.2）：
 * - **符号链接指向上下文之外一律跳过**并记进 `skipped`（防目录穿越）；
 * - `.dockerignore` 只实现常用子集：注释/空行、`!` 取反、`*` 与 `?` 通配、目录、末尾 `/`、
 *   开头的 `/` 锚定；不实现 `**` 的全部细节与字符类（登记为偏离）；
 * - 超过大小或文件数上限立即失败，不留半个 tar。
 *
 * `tempDirectory` 为空时用系统临时目录（测试传自己的目录）。
 */
BuildContextResult packBuildContext(const BuildContextOptions &options, const QString &tempDirectory = {});

/*! 删掉打包产生的临时文件（上传结束/失败后都要调）。 */
void removeArchive(const QString &archivePath);

} // namespace Kontainer
