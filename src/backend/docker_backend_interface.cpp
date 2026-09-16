/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/docker_backend_interface.h"

namespace Kontainer
{

DockerBackendInterface::DockerBackendInterface(QObject *parent)
    : QObject(parent)
{
}

DockerBackendInterface::~DockerBackendInterface() = default;

void DockerBackendInterface::refreshAll()
{
    refreshEngine();
    refreshContainers();
    refreshImages();
}

} // namespace Kontainer
