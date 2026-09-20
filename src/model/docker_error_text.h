/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/docker_error.h"

#include <QString>

namespace Kontainer
{

/*!
 * Presentation categories for error severity (ARCH_V4 §2.2.2).
 *
 * The three classes need different UI treatment and must not collapse into one red error bar:
 *  - UserActionable: the user can fix it (permissions, state conflict, object gone) -> warning + action
 *  - Environment: engine not running, unreachable, timed out, 5xx, version mismatch -> error
 *  - Unexpected: invalid or malformed response -> error plus a hint to attach diagnostics
 */
enum class ErrorCategory {
    None,
    UserActionable,
    Environment,
    Unexpected,
};

/*! DockerError -> user-visible text (ARCH_V1 §6.3: the backend does not own UI strings). */
QString dockerErrorText(const DockerError &error);

/*! Category: QML uses it to pick the InlineMessage type instead of inspecting Kind itself. */
ErrorCategory dockerErrorCategory(const DockerError &error);
/*! Stable category key: none / userActionable / environment / unexpected. */
QString dockerErrorCategoryKey(ErrorCategory category);
inline QString dockerErrorCategoryKey(const DockerError &error)
{
    return dockerErrorCategoryKey(dockerErrorCategory(error));
}

/*!
 * Optional action key (QML maps it to a concrete button):
 * an empty string means no action; `refresh` means refresh.
 */
QString dockerErrorActionKey(const DockerError &error);

} // namespace Kontainer
