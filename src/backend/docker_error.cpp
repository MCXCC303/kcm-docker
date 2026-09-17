/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/docker_error.h"

#include <QAbstractSocket>

namespace Kontainer
{

QString DockerError::kindKey(Kind kind)
{
    switch (kind) {
    case Kind::None:
        return QStringLiteral("none");
    case Kind::DockerUnavailable:
        return QStringLiteral("dockerUnavailable");
    case Kind::ConnectionFailed:
        return QStringLiteral("connectionFailed");
    case Kind::PermissionDenied:
        return QStringLiteral("permissionDenied");
    case Kind::Timeout:
        return QStringLiteral("timeout");
    case Kind::ApiVersionMismatch:
        return QStringLiteral("apiVersionMismatch");
    case Kind::NotFound:
        return QStringLiteral("notFound");
    case Kind::Conflict:
        return QStringLiteral("conflict");
    case Kind::PreconditionFailed:
        return QStringLiteral("preconditionFailed");
    case Kind::HttpError:
        return QStringLiteral("httpError");
    case Kind::EngineError:
        return QStringLiteral("engineError");
    case Kind::InvalidResponse:
        return QStringLiteral("invalidResponse");
    case Kind::UnexpectedPayload:
        return QStringLiteral("unexpectedPayload");
    }
    return QStringLiteral("unknown");
}

DockerError::DockerError(Kind kind, QString detail, int httpStatus)
    : m_kind(kind)
    , m_detail(std::move(detail))
    , m_httpStatus(httpStatus)
{
}

DockerError DockerError::fromHttpStatus(int status, const QString &apiMessage)
{
    DockerError error;
    error.m_httpStatus = status;
    error.m_detail = apiMessage;

    if (status == 401 || status == 403) {
        error.m_kind = Kind::PermissionDenied;
    } else if (status == 404) {
        error.m_kind = Kind::NotFound;
    } else if (status == 409) {
        // 与当前状态冲突：容器正在运行不能删除、镜像被容器引用、名称冲突（ARCH_V4 §2.2.2）
        error.m_kind = Kind::Conflict;
    } else if (status >= 500) {
        error.m_kind = Kind::EngineError;
    } else if (status >= 400) {
        error.m_kind = Kind::HttpError;
    } else {
        error.m_kind = Kind::InvalidResponse;
    }
    return error;
}

DockerError DockerError::fromSocketError(int socketError, const QString &socketErrorString)
{
    DockerError error;
    error.m_detail = socketErrorString;

    switch (static_cast<QAbstractSocket::SocketError>(socketError)) {
    case QAbstractSocket::ConnectionRefusedError:
        error.m_kind = Kind::DockerUnavailable;
        break;
    case QAbstractSocket::SocketAccessError:
        error.m_kind = Kind::PermissionDenied;
        break;
    case QAbstractSocket::SocketTimeoutError:
        error.m_kind = Kind::Timeout;
        break;
    case QAbstractSocket::RemoteHostClosedError:
    case QAbstractSocket::HostNotFoundError:
    case QAbstractSocket::NetworkError:
        error.m_kind = Kind::ConnectionFailed;
        break;
    default:
        error.m_kind = Kind::ConnectionFailed;
        break;
    }
    return error;
}

} // namespace Kontainer
