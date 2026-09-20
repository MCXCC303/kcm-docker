/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "domain/container.h"

#include <QString>

namespace Kontainer
{

/*!
 * State semantics → user-visible text (ARCH_V2 §26: text belongs to the presentation layer;
 * domain objects keep only machine-readable semantics and stable keys).
 */
QString containerStateText(ContainerState state);
QString healthStateText(HealthState health);

} // namespace Kontainer
