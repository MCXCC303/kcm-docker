/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/daemon_config.h"

#include <QObject>
#include <QString>

namespace Kontainer
{

/*!
 * 提权客户端（ARCH_V5_V8 §2.4）。
 *
 * 把"写系统 daemon.json"与"重启 docker.service"包装成两个动作，
 * 通过 KAuth 交给受限 helper 执行：
 *
 *  - **不让整个应用以 root 运行**，只有 helper 在授权后以 root 执行
 *  - helper 未安装 / polkit 不可用 / 用户取消 → 都归一为 `helperUnavailable`
 *    或 `cancelled`，界面据此走"自己动手"的降级路径（功能不静默失败）
 *  - rootless 部署的重启不需要提权：直接与会话总线上的 systemd 通信
 *    （不调用 `systemctl` 二进制，保持"不执行外部程序"的约束）
 */
class PrivilegedConfigClient : public QObject
{
    Q_OBJECT

public:
    enum class Operation {
        WriteConfig,
        Restart,
    };
    Q_ENUM(Operation)

    explicit PrivilegedConfigClient(QObject *parent = nullptr);

    /*! 提权写入是否可用（尽力而为的判断：真正的结论来自执行结果）。 */
    bool writeAvailable() const;

    /*! 发起提权写入；结果经 finished() 回来（异步，不阻塞界面）。 */
    void writeConfig(const DaemonConfigEdits &edits);
    /*! 重启 Docker：系统级走 helper，rootless 走会话 systemd。 */
    void restartDocker(bool systemService);

Q_SIGNALS:
    void finished(Kontainer::PrivilegedConfigClient::Operation operation, bool success, const QString &errorKey);

private:
    void runHelperAction(const QString &actionName, const QVariantMap &arguments, Operation operation);
    void restartViaSessionSystemd();

    bool m_inFlight = false;
};

} // namespace Kontainer
