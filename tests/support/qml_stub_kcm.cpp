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
    , m_hostPaths(new FakeHostPathService(this))
    , m_credentialBackend(new FakeCredentialBackend())
    // 凭据后端注入内存替身：测试与离屏渲染都不该碰用户真实钱包
    , m_controller(new StatusController(backend, m_hostPaths, this, m_credentialBackend))
{
}

QmlStubKcm::~QmlStubKcm()
{
    delete m_credentialBackend;
}

} // namespace Kontainer
