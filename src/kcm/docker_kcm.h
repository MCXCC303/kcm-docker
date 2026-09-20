/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "model/status_controller.h"

#include <KQuickConfigModule>

namespace Kontainer
{

class DockerBackend;
class KioHostPathService;
class PrivilegedConfigClient;

/*!
 * KCM layer (ARCH_V1 §6.1).
 *
 * Responsible only for: KCM lifecycle, QML page loading, exposing presentation models to QML and
 * user-triggered refreshes. It does no URL building, JSON parsing, HTTP status handling or socket
 * access.
 *
 * Phase 1 is a read-only status panel with nothing to save, hence no Apply/Default buttons (§37).
 */
class DockerKcm : public KQuickConfigModule
{
    Q_OBJECT

    Q_PROPERTY(Kontainer::StatusController *controller READ controller CONSTANT)

public:
    DockerKcm(QObject *parent, const KPluginMetaData &metaData);
    ~DockerKcm() override;

    StatusController *controller() const
    {
        return m_controller;
    }

private:
    DockerBackend *m_backend = nullptr;
    /*! Production implementation of "open host folder" for mount rows (ARCH_V4 §2.1.1). */
    KioHostPathService *m_hostPaths = nullptr;
    /*! Restricted privileged client (the helper runs as root, never this process). */
    PrivilegedConfigClient *m_privilegedClient = nullptr;
    StatusController *m_controller = nullptr;
};

} // namespace Kontainer
