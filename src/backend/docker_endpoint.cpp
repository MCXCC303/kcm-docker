/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/docker_endpoint.h"

#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>

namespace Kontainer
{

namespace
{
constexpr auto unixScheme = "unix://";
}

DockerEndpoint DockerEndpoint::unixSocket(const QString &path)
{
    DockerEndpoint endpoint;
    if (path.isEmpty()) {
        endpoint.m_problem = QStringLiteral("no Docker socket path configured");
        return endpoint;
    }
    endpoint.m_type = Type::LocalUnixSocket;
    endpoint.m_socketPath = path;
    return endpoint;
}

DockerEndpoint DockerEndpoint::fromEnvironment()
{
    const QByteArray rawHost = qgetenv("DOCKER_HOST");
    if (!rawHost.isEmpty()) {
        const QString host = QString::fromLocal8Bit(rawHost);
        if (host.startsWith(QLatin1String(unixScheme))) {
            return unixSocket(host.mid(int(sizeof(unixScheme)) - 1));
        }
        DockerEndpoint endpoint;
        // Log the scheme only: DOCKER_HOST may look like tcp://user:pass@host, so the
        // raw value must never reach the log (§25)
        const QString scheme = host.left(host.indexOf(QLatin1String("://")) + 3);
        endpoint.m_problem = QStringLiteral("DOCKER_HOST scheme '%1' is not supported in phase 1 (only unix:// is)").arg(scheme);
        return endpoint;
    }

    // rootless Docker uses $XDG_RUNTIME_DIR/docker.sock, system Docker /run/docker.sock
    // (/var/run is usually just a symlink to /run, kept as the last fallback).
    QStringList candidates;
    const QString runtimeDir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (!runtimeDir.isEmpty()) {
        candidates << runtimeDir + QStringLiteral("/docker.sock");
    }
    candidates << QStringLiteral("/run/docker.sock") << QStringLiteral("/var/run/docker.sock");

    for (const QString &candidate : std::as_const(candidates)) {
        if (QFileInfo::exists(candidate)) {
            return unixSocket(candidate);
        }
    }

    // If none exists, return the default path so connecting fails with a clear
    // "Docker unavailable" error
    return unixSocket(QStringLiteral("/var/run/docker.sock"));
}

QString DockerEndpoint::displayName() const
{
    if (m_type == Type::LocalUnixSocket) {
        return QStringLiteral("unix://") + m_socketPath;
    }
    return {};
}

} // namespace Kontainer
