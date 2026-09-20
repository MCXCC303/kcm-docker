/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/docker_capabilities.h"

#include <QFileInfo>

namespace Kontainer
{

QString writeAccessKey(WriteAccess access)
{
    switch (access) {
    case WriteAccess::Allowed:
        return QStringLiteral("allowed");
    case WriteAccess::SocketMissing:
    case WriteAccess::SocketNotWritable:
        return QStringLiteral("denied");
    case WriteAccess::UnsupportedEndpoint:
        return QStringLiteral("unsupported");
    }
    return QStringLiteral("denied");
}

WriteAccess writeAccessFor(const DockerEndpoint &endpoint)
{
    // Read-only invariant for remote endpoints (ARCH_V3 §1.3). Remote connections are out of
    // scope for now, but this guard lets a future TCP endpoint inherit it.
    if (!endpoint.isValid() || endpoint.type() != DockerEndpoint::Type::LocalUnixSocket) {
        return WriteAccess::UnsupportedEndpoint;
    }

    const QFileInfo socketInfo(endpoint.socketPath());
    if (!socketInfo.exists()) {
        return WriteAccess::SocketMissing;
    }
    // On Unix isWritable() reflects access(2): whether the kernel lets this process write the
    // socket file — exactly the permission needed to connect a unix socket.
    if (!socketInfo.isWritable()) {
        return WriteAccess::SocketNotWritable;
    }
    return WriteAccess::Allowed;
}

} // namespace Kontainer
