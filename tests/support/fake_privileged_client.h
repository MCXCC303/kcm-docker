/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/privileged_client.h"

namespace Kontainer
{

/*!
 * 提权客户端的测试替身（见 `backend/privileged_client.h` 的说明）。
 *
 * 只记录"谁调用了什么"，结果由测试自己 `Q_EMIT finished(...)` 决定：
 * 于是"解锁只作用于发起的作用域""别人的保存结果不该刷新我这一页"
 * 这类时序逻辑可以在没有 root、没有 polkit、没有 helper 的情况下确定性地验证。
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

    /*! `writeAvailable()` 的返回值（默认可用）。 */
    bool available = true;
    int authorizeRequests = 0;
    int writeRequests = 0;
    int restartRequests = 0;
    bool lastSystemService = true;
    DaemonConfigEdits lastEdits;
};

} // namespace Kontainer
