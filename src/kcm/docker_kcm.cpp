/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
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
    // Injected only here: without it (core/tests) the config page falls back to doing it by hand
    m_controller->daemonConfigUser()->setPrivilegedClient(m_privilegedClient);
    m_controller->daemonConfigSystem()->setPrivilegedClient(m_privilegedClient);
    setupTranslationDomain();

    // Read-only status panel: no configuration to save and no mutation buttons
    setButtons(NoAdditionalButton);

    // Exposed to QML (org.kde.kcm.docker). Shared with the QML load test.
    registerKontainerQmlTypes();

    qCDebug(kontainerKcm) << "Kontainer KCM created; build:" << m_controller->buildStamp() << "endpoint:" << m_backend->endpointDisplayName();

    // Refresh as soon as the KCM opens (ARCH_V1 §15)
    m_controller->refresh();
}

DockerKcm::~DockerKcm() = default;

#include "docker_kcm.moc"
