/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/service_control.h"

#include "backend/service_status.h"

namespace Kontainer
{

QString serviceVerbKey(ServiceVerb verb)
{
    switch (verb) {
    case ServiceVerb::Start:
        return QStringLiteral("start");
    case ServiceVerb::Stop:
        return QStringLiteral("stop");
    case ServiceVerb::Restart:
        return QStringLiteral("restart");
    case ServiceVerb::Enable:
        return QStringLiteral("enable");
    case ServiceVerb::Disable:
        return QStringLiteral("disable");
    }
    return {};
}

bool serviceVerbFromKey(const QString &key, ServiceVerb *verb)
{
    if (key == QLatin1String("start")) {
        *verb = ServiceVerb::Start;
        return true;
    }
    if (key == QLatin1String("stop")) {
        *verb = ServiceVerb::Stop;
        return true;
    }
    if (key == QLatin1String("restart")) {
        *verb = ServiceVerb::Restart;
        return true;
    }
    if (key == QLatin1String("enable")) {
        *verb = ServiceVerb::Enable;
        return true;
    }
    if (key == QLatin1String("disable")) {
        *verb = ServiceVerb::Disable;
        return true;
    }
    return false;
}

QStringList managedServiceVerbs()
{
    return {QStringLiteral("start"),
            QStringLiteral("stop"),
            QStringLiteral("restart"),
            QStringLiteral("enable"),
            QStringLiteral("disable")};
}

bool isManagedServiceUnit(const QString &unit)
{
    return managedServiceUnits().contains(unit);
}

QString serviceActionName(ServiceVerb verb)
{
    return QStringLiteral("org.kde.kontainer.service.") + serviceVerbKey(verb);
}

QString serviceHelperSlot(ServiceVerb verb)
{
    return QStringLiteral("service_") + serviceVerbKey(verb);
}

QString serviceControlArgumentError(const QString &unit, const QString &verbKey)
{
    if (unit.isEmpty()) {
        return QStringLiteral("unitRequired");
    }
    if (!isManagedServiceUnit(unit)) {
        return QStringLiteral("unitNotManaged");
    }
    if (verbKey.isEmpty()) {
        return QStringLiteral("verbRequired");
    }
    ServiceVerb verb = ServiceVerb::Start;
    if (!serviceVerbFromKey(verbKey, &verb)) {
        return QStringLiteral("verbNotManaged");
    }
    return {};
}

} // namespace Kontainer
