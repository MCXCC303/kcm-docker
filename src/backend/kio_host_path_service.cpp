/*
    SPDX-FileCopyrightText: 2026 kontainer developers
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
    // 只做一次 stat：不读文件内容、不递归、不解析符号链接之外的任何东西
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

    // 用 OpenUrlJob 而不是 OpenFileManagerWindowJob：后者只是「在父目录里高亮」，
    // 传目录进去不会打开它（KIO 头文件明确写了这一点）。
    // 这里也不记录路径本身：挂载源路径属潜在敏感信息（ARCH_V2 §40）。
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
