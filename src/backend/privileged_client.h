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
 * 提权客户端接口（ARCH_V5_V8 §2.4）。
 *
 * 为什么要有这个接口：`PrivilegedConfigClient` 一调用就真的去激活 helper、
 * 触发 polkit —— 单测里没法驱动它，而"哪个作用域发起的请求、结果该算在谁头上"
 * 恰恰是最容易出错、也最必须钉死的部分（真实踩过：系统级页面解锁把用户级页面
 * 也标成已解锁；一个页面的保存结果让另一个页面弹出"配置已写入"）。
 *
 * 所以控制器只依赖这个接口，测试注入 `FakePrivilegedClient`
 * （见 `tests/support/fake_privileged_client.h`）：调用被记录下来，
 * 结果由测试自己 `Q_EMIT finished(...)` 决定，时序完全确定。
 */
class PrivilegedClient : public QObject
{
    Q_OBJECT

public:
    enum class Operation {
        /*! 只请求授权（界面上的「解锁」）：校验但不写盘。 */
        Authorize,
        WriteConfig,
        Restart,
    };
    Q_ENUM(Operation)

    explicit PrivilegedClient(QObject *parent = nullptr)
        : QObject(parent)
    {
    }
    ~PrivilegedClient() override = default;

    /*! 提权写入是否可用（尽力而为的判断：真正的结论来自执行结果）。 */
    virtual bool writeAvailable() const = 0;

    /*!
     * 请求授权（「解锁」）。
     *
     * 打的是**写配置**那个 action id：polkit 的 keep 按动作记忆，
     * 只有这样才能让随后的保存与重启在 keep 窗口内不再询问。
     */
    virtual void requestAuthorization() = 0;
    /*! 发起提权写入；结果经 finished() 回来（异步，不阻塞界面）。 */
    virtual void writeConfig(const DaemonConfigEdits &edits) = 0;
    /*! 重启 Docker：系统级走 helper，rootless 走会话 systemd。 */
    virtual void restartDocker(bool systemService) = 0;

Q_SIGNALS:
    /*! 操作结果（`errorKey` 为空表示成功）。 */
    void finished(Kontainer::PrivilegedClient::Operation operation, bool success, const QString &errorKey);
};

} // namespace Kontainer
