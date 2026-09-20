/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QLoggingCategory>

/*!
 * Logging categories (ARCH_V1 §27).
 *
 * Release builds stay silent for Docker JSON, environment, credentials, full headers and socket
 * payloads; enable them in development via QT_LOGGING_RULES, e.g.
 *
 *   QT_LOGGING_RULES="kontainer.*.debug=true"
 */
Q_DECLARE_LOGGING_CATEGORY(kontainerBackend)
Q_DECLARE_LOGGING_CATEGORY(kontainerApi)
Q_DECLARE_LOGGING_CATEGORY(kontainerModel)
Q_DECLARE_LOGGING_CATEGORY(kontainerKcm)
