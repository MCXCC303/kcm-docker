/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "domain/container.h"

#include <QString>

namespace Kontainer
{

/*!
 * 状态语义 → 用户可见文案（ARCH_V2 §26：文案属于 presentation 层，
 * domain object 只保留机器可读语义与稳定 key）。
 */
QString containerStateText(ContainerState state);
QString healthStateText(HealthState health);

} // namespace Kontainer
