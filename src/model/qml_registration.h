/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

namespace Kontainer
{

/*!
 * 注册 QML 类型（org.kde.kontainer）。
 *
 * KCM 与 QML 加载测试共用同一份注册代码，避免两边漂移。
 * 幂等：重复调用不会有副作用（Qt 会忽略重复注册）。
 */
void registerKontainerQmlTypes();

} // namespace Kontainer
