/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/daemon_config.h"

#include <QObject>
#include <QString>

namespace Kontainer
{

/*!
 * Privileged client interface (ARCH_V5_V8 §2.4).
 *
 * Why it exists: `PrivilegedConfigClient` really activates the helper and triggers polkit on every
 * call, so unit tests cannot drive it — yet "which scope issued the request, and who owns the
 * result" is the most error-prone part (real bugs: unlocking the system page also marked the user
 * page unlocked; one page's save popped "config written" in another page).
 *
 * So controllers depend on this interface only, and tests inject `FakePrivilegedClient`
 * (see `tests/support/fake_privileged_client.h`): calls are recorded, the test decides the result
 * via `Q_EMIT finished(...)`, and the timing is fully deterministic.
 */
class PrivilegedClient : public QObject
{
    Q_OBJECT

public:
    enum class Operation {
        /*! Authorization only (the UI's "unlock"): validates without writing to disk. */
        Authorize,
        WriteConfig,
        Restart,
        /*! Fixed actions on docker.socket / docker.service / containerd.service (B1). */
        ServiceControl,
    };
    Q_ENUM(Operation)

    explicit PrivilegedClient(QObject *parent = nullptr)
        : QObject(parent)
    {
    }
    ~PrivilegedClient() override = default;

    /*! Whether privileged writes are usable (best effort; the real answer comes from the result). */
    virtual bool writeAvailable() const = 0;

    /*!
     * Request authorization (the UI's "unlock").
     *
     * Uses the **write-config** action id: polkit remembers keeps per action, so only this lets the
     * following save and restart skip prompting inside the keep window.
     */
    virtual void requestAuthorization() = 0;
    /*! Start a privileged write; the result arrives via finished() (async, non-blocking). */
    virtual void writeConfig(const DaemonConfigEdits &edits) = 0;
    /*! Restart Docker: system-wide via the helper, rootless via session systemd. */
    virtual void restartDocker(bool systemService) = 0;
    /*!
     * Control a Docker-related service (B1): `unit` must be whitelisted and `verbKey` one of the
     * five fixed verbs.
     *
     * Validation lives in `serviceControlArgumentError()` (pure function); invalid requests trigger
     * no privileged action.
     */
    virtual void controlService(const QString &unit, const QString &verbKey) = 0;

Q_SIGNALS:
    /*! Operation result (an empty `errorKey` means success). */
    void finished(Kontainer::PrivilegedClient::Operation operation, bool success, const QString &errorKey);
};

} // namespace Kontainer
