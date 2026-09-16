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

} // namespace Kontainer
