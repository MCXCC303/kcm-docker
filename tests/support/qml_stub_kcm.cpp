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
    , m_configDir(new QTemporaryDir())
    // 凭据后端注入内存替身、预设存储指向临时目录：测试与离屏渲染都不该碰用户的真实数据
    , m_mountPresets(new MountPresetStore(m_configDir->filePath(QStringLiteral("kontainerrc"))))
    , m_directoryPicker(new FakeDirectoryPicker())
    , m_serviceStatus(new FakeServiceStatus())
    , m_controller(new StatusController(backend,
                                        m_hostPaths,
                                        this,
                                        m_credentialBackend,
                                        m_mountPresets,
                                        m_directoryPicker,
                                        m_serviceStatus))
{
}

QmlStubKcm::~QmlStubKcm()
{
    delete m_serviceStatus;
    delete m_directoryPicker;
    delete m_mountPresets;
    delete m_configDir;
    delete m_credentialBackend;
}

} // namespace Kontainer
