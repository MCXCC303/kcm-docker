/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "kcm/docker_kcm.h"

#include "backend/docker_backend.h"
#include "backend/kio_host_path_service.h"
#include "backend/privileged_config_client.h"
#include "i18n.h"
#include "logging.h"
#include "model/qml_registration.h"
#include "model/status_controller.h"


using namespace Kontainer;

K_PLUGIN_FACTORY_WITH_JSON(DockerKcmFactory, "kcm_docker.json", registerPlugin<Kontainer::DockerKcm>();)

DockerKcm::DockerKcm(QObject *parent, const KPluginMetaData &metaData)
    : KQuickConfigModule(parent, metaData)
    , m_backend(new DockerBackend(this))
    , m_hostPaths(new KioHostPathService(this))
    , m_privilegedClient(new PrivilegedConfigClient(this))
    , m_controller(new StatusController(m_backend, m_hostPaths, this))
{
    // 提权客户端只在这里注入：core/测试里没有它时，配置页会自动走"自己动手"的降级路径
    m_controller->daemonConfigUser()->setPrivilegedClient(m_privilegedClient);
    m_controller->daemonConfigSystem()->setPrivilegedClient(m_privilegedClient);
    setupTranslationDomain();

    // 只读状态面板：没有需要保存的配置，也没有写操作按钮
    setButtons(NoAdditionalButton);

    // 暴露给 QML（org.kde.kcm.docker）。注册代码与 QML 加载测试共用。
    registerKontainerQmlTypes();

    qCDebug(kontainerKcm) << "Kontainer KCM created; build:" << m_controller->buildStamp() << "endpoint:" << m_backend->endpointDisplayName();

    // 打开 KCM 立即刷新（ARCH_V1 §15）
    m_controller->refresh();
}

DockerKcm::~DockerKcm() = default;

#include "docker_kcm.moc"
