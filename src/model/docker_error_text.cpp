/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/docker_error_text.h"

#include <KLocalizedString>

namespace Kontainer
{

QString dockerErrorText(const DockerError &error)
{
    switch (error.kind()) {
    case DockerError::Kind::None:
        return {};
    case DockerError::Kind::DockerUnavailable:
        return i18n("Docker Engine is not reachable.");
    case DockerError::Kind::ConnectionFailed:
        return i18n("Could not connect to the Docker daemon.");
    case DockerError::Kind::PermissionDenied:
        return i18n("Permission denied while accessing the Docker socket.");
    case DockerError::Kind::Timeout:
        return i18n("The Docker daemon did not respond in time.");
    case DockerError::Kind::ApiVersionMismatch:
        return i18n("This Docker Engine version is not supported by Kontainer.");
    case DockerError::Kind::NotFound:
        return i18n("The requested Docker API endpoint was not found.");
    case DockerError::Kind::HttpError:
        return i18n("The Docker daemon rejected the request.");
    case DockerError::Kind::EngineError:
        return i18n("The Docker daemon reported an internal error.");
    case DockerError::Kind::InvalidResponse:
        return i18n("The Docker daemon returned an unreadable response.");
    case DockerError::Kind::UnexpectedPayload:
        return i18n("The Docker daemon returned unexpected data.");
    }
    return i18n("Unknown Docker error.");
}

} // namespace Kontainer
