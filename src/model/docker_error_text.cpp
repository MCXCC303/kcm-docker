/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/docker_error_text.h"

#include <KLocalizedString>

namespace Kontainer
{

namespace
{

/*!
 * The three 409 meanings (ARCH_V4 §2.2.2) can only be told apart by the engine message: the HTTP
 * status itself carries no such detail, and the text must tell the user what to do next.
 * Matches Docker's long-stable English wording; on no match it falls back to generic text.
 */
QString conflictText(const DockerError &error)
{
    const QString detail = error.detail();

    if (detail.contains(QLatin1String("is running"), Qt::CaseInsensitive)
        || detail.contains(QLatin1String("container is running"), Qt::CaseInsensitive)) {
        return i18n("The container is running. Stop it before deleting it.");
    }
    if (detail.contains(QLatin1String("is using its referenced image"), Qt::CaseInsensitive)
        || detail.contains(QLatin1String("image is being used by"), Qt::CaseInsensitive)
        || detail.contains(QLatin1String("image is in use by"), Qt::CaseInsensitive)) {
        return i18n("The image is in use by a container and cannot be deleted.");
    }
    if (detail.contains(QLatin1String("referenced in multiple repositories"), Qt::CaseInsensitive)) {
        return i18n("The image has more than one tag. Force deletion removes all of its tags.");
    }
    if (detail.contains(QLatin1String("already in use"), Qt::CaseInsensitive)
        || detail.contains(QLatin1String("Conflict. The container name"), Qt::CaseInsensitive)) {
        return i18n("The name is already in use.");
    }
    return i18n("The Docker daemon rejected the request because of the current state.");
}

} // namespace

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
        return i18n("The object no longer exists. It may have been removed outside Kontainer.");
    case DockerError::Kind::Conflict:
        return conflictText(error);
    case DockerError::Kind::PreconditionFailed:
        return i18n("The action was not started because a precondition was not met.");
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

ErrorCategory dockerErrorCategory(const DockerError &error)
{
    switch (error.kind()) {
    case DockerError::Kind::None:
        return ErrorCategory::None;
    case DockerError::Kind::PermissionDenied:
    case DockerError::Kind::Conflict:
    case DockerError::Kind::PreconditionFailed:
    case DockerError::Kind::NotFound:
        return ErrorCategory::UserActionable;
    case DockerError::Kind::DockerUnavailable:
    case DockerError::Kind::ConnectionFailed:
    case DockerError::Kind::Timeout:
    case DockerError::Kind::ApiVersionMismatch:
    case DockerError::Kind::HttpError:
    case DockerError::Kind::EngineError:
        return ErrorCategory::Environment;
    case DockerError::Kind::InvalidResponse:
    case DockerError::Kind::UnexpectedPayload:
        return ErrorCategory::Unexpected;
    }
    return ErrorCategory::Unexpected;
}

QString dockerErrorCategoryKey(ErrorCategory category)
{
    switch (category) {
    case ErrorCategory::None:
        return QStringLiteral("none");
    case ErrorCategory::UserActionable:
        return QStringLiteral("userActionable");
    case ErrorCategory::Environment:
        return QStringLiteral("environment");
    case ErrorCategory::Unexpected:
        return QStringLiteral("unexpected");
    }
    return QStringLiteral("unexpected");
}

QString dockerErrorActionKey(const DockerError &error)
{
    // When the object is gone, refreshing to the real list state is the only useful action
    if (error.kind() == DockerError::Kind::NotFound) {
        return QStringLiteral("refresh");
    }
    return {};
}

} // namespace Kontainer
