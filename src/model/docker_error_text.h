/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/docker_error.h"

#include <QString>

namespace Kontainer
{

/*!
 * DockerError → 用户可见文本（ARCH_V1 §6.3：backend 不负责 UI 文本）。
 *
 * backend 只提供错误分类 Kind 与不可翻译的技术 detail；可翻译文案由
 * presentation layer 在这里统一生成，QML 通过 StatusController 拿到结果。
 */
QString dockerErrorText(const DockerError &error);

} // namespace Kontainer
