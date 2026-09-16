/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

namespace Kontainer
{

/*!
 * 翻译域：与 KCM 插件 id 保持一致（KDE 惯例：译文安装为
 * share/locale/<lang>/LC_MESSAGES/kcm_<name>.mo）。
 * po/<lang>/<domain>.po 会编译成同名 .mo。
 */
inline constexpr auto kTranslationDomain = "kcm_docker";

/*!
 * 设置 KDE 翻译域（幂等）。
 *
 * 所有面向用户的字符串都通过 KDE i18n 生成（ARCH_V1 §22），i18n 宏要求先设置
 * 翻译域。KCM 启动时（以及测试 main()）调用一次即可。
 */
void setupTranslationDomain();

} // namespace Kontainer
