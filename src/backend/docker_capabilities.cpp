/*
    SPDX-FileCopyrightText: 2026 kontainer developers
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
    // 远程 endpoint 的只读不变式（ARCH_V3 §1.3）：四期不实现远程连接，
    // 但这条判断先写在这里，未来加 TCP endpoint 时自动继承。
    if (!endpoint.isValid() || endpoint.type() != DockerEndpoint::Type::LocalUnixSocket) {
        return WriteAccess::UnsupportedEndpoint;
    }

    const QFileInfo socketInfo(endpoint.socketPath());
    if (!socketInfo.exists()) {
        return WriteAccess::SocketMissing;
    }
    // Unix 上 isWritable() 反映 access(2) 的结果，也就是内核认为当前进程
    // 能不能写这个 socket 文件——正是连接 Unix socket 所需的权限。
    if (!socketInfo.isWritable()) {
        return WriteAccess::SocketNotWritable;
    }
    return WriteAccess::Allowed;
}

} // namespace Kontainer
