/*
    SPDX-FileCopyrightText: 2026 kontainer developers
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
    // Docker 以 `name=rootless` 标记 rootless 模式（SecurityOptions 里的字符串）
    for (const QString &option : m_info.securityOptions) {
        if (option.contains(QLatin1String("rootless"))) {
            return true;
        }
    }
    return false;
}

} // namespace Kontainer
