/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/engine_status.h"

namespace Kontainer
{

EngineStatus::EngineStatus(QObject *parent)
    : QObject(parent)
{
}

void EngineStatus::setInfo(const EngineInfo &info)
{
    m_info = info;
    Q_EMIT changed();
}

void EngineStatus::clear()
{
    m_info = EngineInfo();
    Q_EMIT changed();
}

bool EngineStatus::rootless() const
{
    // Docker marks rootless mode with `name=rootless` (a string in SecurityOptions)
    for (const QString &option : m_info.securityOptions) {
        if (option.contains(QLatin1String("rootless"))) {
            return true;
        }
    }
    return false;
}

} // namespace Kontainer
