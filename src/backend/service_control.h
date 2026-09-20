/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QString>
#include <QStringList>

namespace Kontainer
{

/*!
 * Actions allowed on a systemd service (ARCH_V5_V8 §B1).
 *
 * Exactly these five **fixed verbs**; arbitrary systemctl arguments are not accepted.
 */
enum class ServiceVerb {
    Start,
    Stop,
    Restart,
    Enable,
    Disable,
};

/*! Stable key (UI and tests use it) -> `start` / `stop` / `restart` / `enable` / `disable`. */
QString serviceVerbKey(ServiceVerb verb);
/*! key -> enum; returns false when unknown (callers reject on that). */
bool serviceVerbFromKey(const QString &key, ServiceVerb *verb);
/*! All verbs (used to lay out UI buttons and to iterate in tests). */
QStringList managedServiceVerbs();

/*! Whether the unit is on our managed whitelist (`managedServiceUnits()`). */
bool isManagedServiceUnit(const QString &unit);

/*! KAuth action name for a verb (`org.kde.kcm.docker.service.start`). */
QString serviceActionName(ServiceVerb verb);
/*! Helper slot name for a verb (`service_start`). */
QString serviceHelperSlot(ServiceVerb verb);

/*!
 * Validate a "control service" request and return a **stable error key** (empty = accepted).
 *
 * This is the most important logic on the privilege boundary, hence a pure, unit-testable function:
 *  - `unitNotManaged`: unit not on the whitelist (rejects arbitrary unit names)
 *  - `verbNotManaged`: unknown verb
 *  - `unitRequired` / `verbRequired`: empty value
 *
 * Both the session side and the helper call it (defense in depth): even if the session side is
 * bypassed, the helper executes nothing outside the whitelist.
 */
QString serviceControlArgumentError(const QString &unit, const QString &verbKey);

} // namespace Kontainer
