/*
    SPDX-FileCopyrightText: 2026 kontainer developers
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
        // 只记录 scheme：DOCKER_HOST 可能形如 tcp://user:pass@host，不能把原值写进日志（§25）
        const QString scheme = host.left(host.indexOf(QLatin1String("://")) + 3);
        endpoint.m_problem = QStringLiteral("DOCKER_HOST scheme '%1' is not supported in phase 1 (only unix:// is)").arg(scheme);
        return endpoint;
    }

    // rootless Docker 使用 $XDG_RUNTIME_DIR/docker.sock，系统级 Docker 使用 /run/docker.sock
    // （/var/run 通常只是 /run 的符号链接，这里保留为最后兜底）。
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

    // 都不存在时给出默认路径，让连接阶段产生明确的 “Docker 不可用” 错误
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
