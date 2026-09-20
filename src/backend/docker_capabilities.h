/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/docker_endpoint.h"

#include <QString>

namespace Kontainer
{

/*!
 * Write-permission gate (ARCH_V4 §2.2.3).
 *
 * Model (ARCH_V3 §1.3): **work with the socket's real permissions, never escalate**. One cheap
 * check, nothing but I/O:
 *  - endpoint must be a local unix socket (remote endpoints are always read-only)
 *  - socket file must exist
 *  - socket must be writable by this process (QFileInfo::isWritable() is access(2) on Unix)
 *
 * Not writable means no write entry points in the UI. The engine stays authoritative: a mutation
 * returning 403 / EACCES downgrades the session to read-only (OperationController).
 */
enum class WriteAccess {
    Allowed,
    /*! Socket missing: the engine is probably not running. */
    SocketMissing,
    /*! Socket not writable: user is not in the socket's group, or permissions were tightened. */
    SocketNotWritable,
    /*! Not a local unix socket (remote endpoint): read-only by design. */
    UnsupportedEndpoint,
};

/*! Stable key: allowed / denied / unsupported (QML uses it to render write entry points). */
QString writeAccessKey(WriteAccess access);
/*! Whether write entry points may be shown. */
inline bool writeAccessAllowed(WriteAccess access)
{
    return access == WriteAccess::Allowed;
}

WriteAccess writeAccessFor(const DockerEndpoint &endpoint);

} // namespace Kontainer
