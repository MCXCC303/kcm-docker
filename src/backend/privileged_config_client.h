/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/privileged_client.h"

#include <QObject>
#include <QString>

namespace Kontainer
{

/*!
 * 基于 KAuth 的提权客户端（ARCH_V5_V8 §2.4）。
 *
 * 把"写系统 daemon.json"与"重启 docker.service"包装成两个动作，
 * 通过 KAuth 交给受限 helper 执行：
 *
 *  - **不让整个应用以 root 运行**，只有 helper 在授权后以 root 执行
 *  - helper 未安装 / polkit 不可用 / 用户取消 → 都归一为 `helperUnavailable`
 *    或 `cancelled`，界面据此走"自己动手"的降级路径（功能不静默失败）
 *  - rootless 部署的重启不需要提权：直接与会话总线上的 systemd 通信
 *    （不调用 `systemctl` 二进制，保持"不执行外部程序"的约束）
 *
 * 接口在 `PrivilegedClient`：控制器只依赖接口，单测注入 fake（见该头文件的说明）。
 */
class PrivilegedConfigClient : public PrivilegedClient
{
    Q_OBJECT

public:
    /*! 与接口同一套操作枚举（保留 `PrivilegedConfigClient::Operation::X` 的写法）。 */
    using Operation = PrivilegedClient::Operation;

    explicit PrivilegedConfigClient(QObject *parent = nullptr);

    bool writeAvailable() const override;
    void requestAuthorization() override;
    void writeConfig(const DaemonConfigEdits &edits) override;
    void restartDocker(bool systemService) override;

private:
    void runHelperAction(const QString &actionName, const QVariantMap &arguments, Operation operation);
    void restartViaSessionSystemd();

    bool m_inFlight = false;
};

} // namespace Kontainer
