/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
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
    , m_configDir(new QTemporaryDir())
    // In-memory credential backend, temp-dir preset store: tests never touch real user data
    , m_mountPresets(new MountPresetStore(m_configDir->filePath(QStringLiteral("kcm_dockerrc"))))
    , m_directoryPicker(new FakeDirectoryPicker())
    , m_serviceStatus(new FakeServiceStatus())
    , m_privilegedClient(new FakePrivilegedClient())
    , m_controller(new StatusController(backend,
                                        m_hostPaths,
                                        this,
                                        m_credentialBackend,
                                        m_mountPresets,
                                        m_directoryPicker,
                                        m_serviceStatus))
{
    // Wiring matches docker_kcm.cpp: the privileged client is injected only at this level
    // (without it in core, the config pages fall back to the "do it yourself" path)
    m_controller->daemonConfigUser()->setPrivilegedClient(m_privilegedClient);
    m_controller->daemonConfigSystem()->setPrivilegedClient(m_privilegedClient);
}

QmlStubKcm::~QmlStubKcm()
{
    delete m_privilegedClient;
    delete m_serviceStatus;
    delete m_directoryPicker;
    delete m_mountPresets;
    delete m_configDir;
    delete m_credentialBackend;
}

} // namespace Kontainer
