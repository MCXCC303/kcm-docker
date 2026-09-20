/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/privileged_client.h"

namespace Kontainer
{

/*!
 * Test double for the privileged client (see `backend/privileged_client.h`).
 *
 * It only records what was called; the test decides outcomes by emitting `finished(...)` itself.
 * That makes ordering rules — "an unlock affects only the scope that requested it", "someone
 * else's save result must not refresh my page" — verifiable deterministically without root,
 * polkit or the helper.
 */
class FakePrivilegedClient : public PrivilegedClient
{
    Q_OBJECT

public:
    explicit FakePrivilegedClient(QObject *parent = nullptr)
        : PrivilegedClient(parent)
    {
    }

    bool writeAvailable() const override
    {
        return available;
    }

    void requestAuthorization() override
    {
        ++authorizeRequests;
    }

    void writeConfig(const DaemonConfigEdits &edits) override
    {
        ++writeRequests;
        lastEdits = edits;
    }

    void restartDocker(bool systemService) override
    {
        ++restartRequests;
        lastSystemService = systemService;
    }

    void controlService(const QString &unit, const QString &verbKey) override
    {
        ++serviceRequests;
        lastServiceUnit = unit;
        lastServiceVerb = verbKey;
    }

    /*! Return value of `writeAvailable()` (available by default). */
    bool available = true;
    int authorizeRequests = 0;
    int writeRequests = 0;
    int restartRequests = 0;
    /*! Service control requests (B1): count plus the most recent unit/verb. */
    int serviceRequests = 0;
    QString lastServiceUnit;
    QString lastServiceVerb;
    bool lastSystemService = true;
    DaemonConfigEdits lastEdits;
};

} // namespace Kontainer
