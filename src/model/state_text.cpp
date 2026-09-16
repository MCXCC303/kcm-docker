/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/state_text.h"

#include <KLocalizedString>

namespace Kontainer
{

QString containerStateText(ContainerState state)
{
    switch (state) {
    case ContainerState::Created:
        return i18n("Created");
    case ContainerState::Restarting:
        return i18n("Restarting");
    case ContainerState::Running:
        return i18n("Running");
    case ContainerState::Removing:
        return i18n("Removing");
    case ContainerState::Paused:
        return i18n("Paused");
    case ContainerState::Exited:
        return i18n("Exited");
    case ContainerState::Dead:
        return i18n("Dead");
    case ContainerState::Unknown:
        break;
    }
    return i18n("Unknown");
}

QString healthStateText(HealthState health)
{
    switch (health) {
    case HealthState::None:
        return i18n("No health check");
    case HealthState::Starting:
        return i18n("Starting");
    case HealthState::Healthy:
        return i18n("Healthy");
    case HealthState::Unhealthy:
        return i18n("Unhealthy");
    case HealthState::Unknown:
        break;
    }
    return QString();
}

} // namespace Kontainer
