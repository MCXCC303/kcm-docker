/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/privileged_client.h"

#include <QObject>
#include <QString>

namespace Kontainer
{

/*!
 * KAuth-based privileged client (ARCH_V5_V8 §2.4).
 *
 * Wraps "write the system daemon.json" and "restart docker.service" into two actions executed by a
 * restricted helper via KAuth:
 *
 *  - **the whole app never runs as root**; only the helper runs as root, after authorization
 *  - missing helper / polkit unavailable / user cancelled all map to `helperUnavailable` or
 *    `cancelled`, so the UI can fall back to "do it yourself" instead of failing silently
 *  - restarting a rootless deployment needs no privilege: it talks to session-bus systemd
 *    directly (never the `systemctl` binary, keeping the "no external programs" constraint)
 *
 * The interface is `PrivilegedClient`: controllers depend only on it and tests inject a fake
 * (see that header).
 */
class PrivilegedConfigClient : public PrivilegedClient
{
    Q_OBJECT

public:
    /*! Same operation enum as the interface (keeps the `PrivilegedConfigClient::Operation::X` spelling). */
    using Operation = PrivilegedClient::Operation;

    explicit PrivilegedConfigClient(QObject *parent = nullptr);

    bool writeAvailable() const override;
    void requestAuthorization() override;
    void writeConfig(const DaemonConfigEdits &edits) override;
    void restartDocker(bool systemService) override;
    void controlService(const QString &unit, const QString &verbKey) override;

private:
    void runHelperAction(const QString &actionName, const QVariantMap &arguments, Operation operation);
    void restartViaSessionSystemd();

    bool m_inFlight = false;
};

} // namespace Kontainer
