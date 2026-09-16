/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "domain/container.h"

#include <QTimeZone>

namespace Kontainer
{

namespace
{
constexpr int shortIdLength = 12;
}

ContainerState containerStateFromString(const QString &state)
{
    const QString normalized = state.trimmed().toLower();
    if (normalized == QLatin1String("created")) {
        return ContainerState::Created;
    }
    if (normalized == QLatin1String("restarting")) {
        return ContainerState::Restarting;
    }
    if (normalized == QLatin1String("running")) {
        return ContainerState::Running;
    }
    if (normalized == QLatin1String("removing")) {
        return ContainerState::Removing;
    }
    if (normalized == QLatin1String("paused")) {
        return ContainerState::Paused;
    }
    if (normalized == QLatin1String("exited")) {
        return ContainerState::Exited;
    }
    if (normalized == QLatin1String("dead")) {
        return ContainerState::Dead;
    }
    return ContainerState::Unknown;
}

QString containerStateKey(ContainerState state)
{
    switch (state) {
    case ContainerState::Created:
        return QStringLiteral("created");
    case ContainerState::Restarting:
        return QStringLiteral("restarting");
    case ContainerState::Running:
        return QStringLiteral("running");
    case ContainerState::Removing:
        return QStringLiteral("removing");
    case ContainerState::Paused:
        return QStringLiteral("paused");
    case ContainerState::Exited:
        return QStringLiteral("exited");
    case ContainerState::Dead:
        return QStringLiteral("dead");
    case ContainerState::Unknown:
        break;
    }
    return QStringLiteral("unknown");
}

HealthState healthStateFromString(const QString &health)
{
    const QString normalized = health.trimmed().toLower();
    if (normalized == QLatin1String("none")) {
        return HealthState::None;
    }
    if (normalized == QLatin1String("starting")) {
        return HealthState::Starting;
    }
    if (normalized == QLatin1String("healthy")) {
        return HealthState::Healthy;
    }
    if (normalized == QLatin1String("unhealthy")) {
        return HealthState::Unhealthy;
    }
    return HealthState::Unknown;
}

QString healthStateKey(HealthState health)
{
    switch (health) {
    case HealthState::None:
        return QStringLiteral("none");
    case HealthState::Starting:
        return QStringLiteral("starting");
    case HealthState::Healthy:
        return QStringLiteral("healthy");
    case HealthState::Unhealthy:
        return QStringLiteral("unhealthy");
    case HealthState::Unknown:
        break;
    }
    return QStringLiteral("unknown");
}

QString Container::shortId() const
{
    return id.left(shortIdLength);
}
QString Container::stateKey() const
{
    return containerStateKey(state);
}

QString Container::healthKey() const
{
    return healthStateKey(health);
}

} // namespace Kontainer
