/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QLoggingCategory>

/*!
 * 日志分类（ARCH_V1 §27）。
 *
 * 默认 release 构建不输出 Docker JSON、environment、认证材料、完整 header 与
 * socket payload；开发时可用 QT_LOGGING_RULES 打开，例如：
 *
 *   QT_LOGGING_RULES="kontainer.*.debug=true"
 */
Q_DECLARE_LOGGING_CATEGORY(kontainerBackend)
Q_DECLARE_LOGGING_CATEGORY(kontainerApi)
Q_DECLARE_LOGGING_CATEGORY(kontainerModel)
Q_DECLARE_LOGGING_CATEGORY(kontainerKcm)
