/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "support/qml_stub_kcm.h"

#include "model/status_controller.h"

namespace Kontainer
{

QmlStubKcm::QmlStubKcm(DockerBackendInterface *backend, QObject *parent)
    : QObject(parent)
    , m_controller(new StatusController(backend, this))
{
}

QmlStubKcm::~QmlStubKcm() = default;

} // namespace Kontainer
