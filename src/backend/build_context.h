/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QString>
#include <QStringList>

namespace Kontainer
{

/*!
 * Build-context options for packaging (ARCH_V5_V8 §5.2).
 */
struct BuildContextOptions {
    /*! Context directory (user-chosen; must exist). */
    QString directory;
    /*! Dockerfile name, relative to the context directory (default `Dockerfile`). */
    QString dockerfile = QStringLiteral("Dockerfile");
    /*! Inline Dockerfile content: when set, written into the context instead of read from disk (§5.4). */
    QString inlineDockerfile;
    /*! Context size limit in bytes. */
    qint64 maxBytes = 512LL * 1024 * 1024;
    /*! File-count limit. */
    int maxFiles = 20000;
};

/*!
 * Packaging result.
 *
 * On failure `errorKey` is a stable key (text lives in the UI): `contextMissing` / `notADirectory` /
 * `dockerfileMissing` / `contextTooLarge` / `tooManyFiles` / `archiveFailed`.
 * Failures leave **no temp files** behind (a partially written tar is deleted).
 */
struct BuildContextResult {
    bool ok = false;
    /*! Packed tar; the caller deletes it after upload via `removeArchive()`. */
    QString archivePath;
    qint64 bytes = 0;
    int fileCount = 0;
    /*! Skipped entries (escaping symlinks; .dockerignore hits are not counted), for UI hints. */
    QStringList skipped;
    QString errorKey;
    QString errorDetail;
};

/*!
 * Pack a directory into a tar (uncompressed `application/x-tar`; Docker accepts that).
 *
 * Safety and limits (§5.2):
 * - **Symlinks escaping the context are always skipped** and listed in `skipped` (anti-traversal);
 * - `.dockerignore` implements only a common subset: comments/blank lines, `!` negation, `*` and `?`
 *   globs, directories, trailing `/`, leading `/` anchoring; full `**` details and character classes
 *   are not implemented (logged as a deviation);
 * - Exceeding the size or file-count limit fails immediately, leaving no half-written tar.
 *
 * An empty `tempDirectory` means the system temp dir (tests pass their own).
 */
BuildContextResult packBuildContext(const BuildContextOptions &options, const QString &tempDirectory = {});

/*! Delete the temp file produced by packing (call after upload, success or failure). */
void removeArchive(const QString &archivePath);

} // namespace Kontainer
