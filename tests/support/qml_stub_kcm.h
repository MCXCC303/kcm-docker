/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/docker_backend_interface.h"
#include "support/fake_credential_backend.h"
#include "support/fake_host_path_service.h"
#include "support/fake_privileged_client.h"
#include "backend/directory_picker.h"
#include "model/mount_preset_store.h"
#include "model/status_controller.h"
#include "support/mock_docker_backend.h"

#include <QObject>
#include <QTemporaryDir>
#include <QString>

namespace Kontainer
{

/*!
 * KCM stub for QML load tests (ARCH_V2 §46).
 *
 * Provides the `kcm` context properties the QML uses (controller / name / description) so UI files
 * can be loaded and checked for errors without kcmshell6.
 */
class QmlStubKcm : public QObject
{
    Q_OBJECT

    Q_PROPERTY(Kontainer::StatusController *controller READ controller CONSTANT)
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString description READ description CONSTANT)

public:
    explicit QmlStubKcm(DockerBackendInterface *backend, QObject *parent = nullptr);

    /*!
     * In-memory credential backend (used by tests and offscreen rendering).
     *
     * Never the real KWallet: that pops an unlock dialog and leaves entries in the user's wallet.
     * Cases needing preset credentials insert into `entries` directly or drive the login flow.
     */
    FakeCredentialBackend *credentialBackend() const
    {
        return m_credentialBackend;
    }
    /*! Directory picker double: no dialog, returns a preset path (empty string = user cancelled). */
    class FakeDirectoryPicker : public DirectoryPicker
    {
    public:
        explicit FakeDirectoryPicker(QObject *parent = nullptr)
            : DirectoryPicker(parent)
        {
        }
        QString chooseDirectory(const QString &startPath) override
        {
            lastStartPath = startPath;
            return nextResult;
        }
        QString nextResult;
        QString lastStartPath;
        int callCount = 0;
    };

    FakeDirectoryPicker *directoryPicker() const
    {
        return m_directoryPicker;
    }

    /*! Service status double: all three units run by default (tests can flip one to not running). */
    FakeServiceStatus *serviceStatus() const
    {
        return m_serviceStatus;
    }

    /*! Privileged client double; service-card cases assert that requests follow confirmation only. */
    FakePrivilegedClient *privilegedClient() const
    {
        return m_privilegedClient;
    }

    /*!
     * Mount preset store (pointed at a temp dir).
     *
     * Never the real `~/.config/kcm_dockerrc`: tests add and remove presets in it.
     */
    MountPresetStore *mountPresets() const
    {
        return m_mountPresets;
    }

    /*! Tests can inject probe results and open failures (the mount section's two states). */
    FakeHostPathService *hostPaths() const
    {
        return m_hostPaths;
    }
    ~QmlStubKcm() override;

    StatusController *controller() const
    {
        return m_controller;
    }
    QString name() const
    {
        return QStringLiteral("Docker");
    }
    QString description() const
    {
        return QStringLiteral("Test stub KCM");
    }

private:
    FakeHostPathService *m_hostPaths = nullptr;
    FakeCredentialBackend *m_credentialBackend = nullptr;
    QTemporaryDir *m_configDir = nullptr;
    MountPresetStore *m_mountPresets = nullptr;
    FakeDirectoryPicker *m_directoryPicker = nullptr;
    FakeServiceStatus *m_serviceStatus = nullptr;
    FakePrivilegedClient *m_privilegedClient = nullptr;
    StatusController *m_controller = nullptr;
};

} // namespace Kontainer
