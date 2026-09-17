/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/host_path_service.h"

namespace Kontainer
{

HostPathService::HostPathService(QObject *parent)
    : QObject(parent)
{
}

HostPathService::~HostPathService() = default;

} // namespace Kontainer
