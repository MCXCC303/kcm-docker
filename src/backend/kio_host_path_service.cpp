/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/kio_host_path_service.h"

#include "logging.h"

#include <QFileInfo>

#include <KIO/OpenUrlJob>

namespace Kontainer
{

KioHostPathService::KioHostPathService(QObject *parent)
    : HostPathService(parent)
{
}

KioHostPathService::~KioHostPathService() = default;

HostPathState KioHostPathService::probe(const QString &path) const
{
    if (path.isEmpty()) {
        return HostPathState::NotApplicable;
    }
    // One stat only: no file contents, no recursion, nothing beyond resolving symlinks
    const QFileInfo info(path);
    if (!info.exists()) {
        return HostPathState::Missing;
    }
    return info.isDir() ? HostPathState::Directory : HostPathState::NotADirectory;
}

bool KioHostPathService::openDirectory(const QString &path)
{
    const HostPathState state = probe(path);
    if (state == HostPathState::Missing) {
        Q_EMIT openFinished(HostPathError::Missing, QStringLiteral("path does not exist"));
        return false;
    }
    if (state == HostPathState::NotADirectory) {
        Q_EMIT openFinished(HostPathError::NotADirectory, QStringLiteral("path is not a directory"));
        return false;
    }
    if (state != HostPathState::Directory) {
        Q_EMIT openFinished(HostPathError::NotADirectory, QStringLiteral("no host path for this mount"));
        return false;
    }

    // Use OpenUrlJob, not OpenFileManagerWindowJob: the latter only highlights inside the parent
    // directory and will not open a directory passed to it (stated in the KIO headers).
    // The path itself is not logged either: mount sources are potentially sensitive (ARCH_V2 §40).
    auto *job = new KIO::OpenUrlJob(QUrl::fromLocalFile(path), this);
    job->setShowOpenOrExecuteDialog(false);
    connect(job, &KJob::result, this, [this, job] {
        const bool failed = job->error() != 0;
        if (failed) {
            qCWarning(kontainerModel) << "opening the host directory failed:" << job->errorString();
        }
        Q_EMIT openFinished(failed ? HostPathError::LaunchFailed : HostPathError::None,
                            failed ? job->errorString() : QString());
        job->deleteLater();
    });
    job->start();
    return true;
}

} // namespace Kontainer
