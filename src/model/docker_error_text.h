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
 * 错误分级的呈现分类（ARCH_V4 §2.2.2）。
 *
 * 三类的 UI 处理不同，不允许混成同一个红色报错条：
 *  - UserActionable：用户自己能解决（权限、状态冲突、目标已消失）→ 警告 + 引导动作
 *  - Environment：环境问题（引擎没跑、连不上、超时、5xx、版本不匹配）→ 错误
 *  - Unexpected：响应非法或结构不符 → 错误 + 建议附带诊断信息
 */
enum class ErrorCategory {
    None,
    UserActionable,
    Environment,
    Unexpected,
};

/*! DockerError → 用户可见文案（ARCH_V1 §6.3：backend 不负责 UI 文本）。 */
QString dockerErrorText(const DockerError &error);

/*! 分级：QML 用它决定 InlineMessage 的类型，不自己判断 Kind。 */
ErrorCategory dockerErrorCategory(const DockerError &error);
/*! 分级的稳定 key：none / userActionable / environment / unexpected。 */
QString dockerErrorCategoryKey(ErrorCategory category);
inline QString dockerErrorCategoryKey(const DockerError &error)
{
    return dockerErrorCategoryKey(dockerErrorCategory(error));
}

/*!
 * 可选的引导动作 key（QML 映射到具体按钮）：
 * 空字符串表示没有可执行动作；`refresh` 表示「刷新」。
 */
QString dockerErrorActionKey(const DockerError &error);

} // namespace Kontainer
