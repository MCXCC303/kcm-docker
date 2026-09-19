/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    受限提权 helper（ARCH_V5_V8 §2.4）。

    它只做两件事，而且只按固定路径做：

      1. `org.kde.kcm.docker.daemon.save`：把白名单键的编辑合并进
         `/etc/docker/daemon.json`（读取由 helper 自己做，调用方给不了任意内容）
      2. `org.kde.kcm.docker.daemon.restart`：通过 **systemd 的 D-Bus 接口**重启 `docker.service`

    槽名不是随便起的：KAuth 按"动作名去掉 helper id 前缀、`.` 换成 `_`"来查槽
    （见 KAuth 的 DBusHelperProxy），所以上面两个动作对应 daemon_save / daemon_restart。
    名字写错不会编译失败，只会在真机上表现为"没有这个动作"。

    刻意不提供的能力（否则就是提权后门）：
      - 不接受路径参数（路径是编译期常量）
      - 不接受任意 JSON / 任意键
      - 不执行任何外部命令（重启走 D-Bus，不用 `systemctl` 二进制）
      - 不做"运行任意命令"的通用接口
*/

#include "backend/daemon_config.h"
#include "backend/service_control.h"
#include "kauth/privileged_config_request.h"
#include "logging.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QFile>
#include <QObject>
#include <QTimer>

#include <KAuth/ActionReply>
#include <KAuth/HelperSupport>

using namespace KAuth;

namespace Kontainer
{

namespace
{
/*! 固定路径：helper 只碰这一个文件。 */
constexpr auto kDaemonConfigPath = "/etc/docker/daemon.json";
/*! 固定单元名：helper 只重启这一个单元。 */
constexpr auto kDockerUnit = "docker.service";

QByteArray readConfigFile()
{
    QFile file(QString::fromLatin1(kDaemonConfigPath));
    if (!file.exists()) {
        return {};
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}
} // namespace

class KontainerHelper : public QObject
{
    Q_OBJECT

private:
    /*!
     * 五个服务动作的共用实现（定义见下方 private 区）。
     *
     * 只接受 `unit` 一个参数，且必须命中 `managedServiceUnits()` 白名单——
     * 这样即使有人绕过会话侧直接调用 D-Bus 动作，也执行不了白名单之外的东西。
     */
    ActionReply runServiceAction(ServiceVerb verb, const QVariantMap &arguments);

public Q_SLOTS:
    /*! 写入 daemon.json（白名单键的编辑意图）。对应动作 org.kde.kcm.docker.daemon.save。 */
    ActionReply daemon_save(const QVariantMap &arguments)
    {
        PrivilegedConfigRequest request;
        QString errorKey;
        if (!PrivilegedConfigRequest::fromArguments(arguments, &request, &errorKey)) {
            qCWarning(kontainerModel) << "privileged config request rejected:" << errorKey;
            return ActionReply::HelperErrorReply(static_cast<int>(ErrorCode::InvalidRequest));
        }

        if (request.dryRun()) {
            // 「解锁」路径：只做授权与校验，绝不写盘
            return ActionReply::SuccessReply();
        }

        const QByteArray existing = readConfigFile();
        const QByteArray merged = request.mergeInto(existing);
        if (merged.isEmpty()) {
            // 既有文件不可解析：拒绝写入而不是覆盖（与界面侧同一条规则）
            return ActionReply::HelperErrorReply(static_cast<int>(ErrorCode::UnparsableConfig));
        }

        QString backupPath;
        const QString error = DaemonConfigWriter::writeAtomically(QString::fromLatin1(kDaemonConfigPath), merged, &backupPath);
        if (!error.isEmpty()) {
            qCWarning(kontainerModel) << "helper failed to write daemon config:" << error;
            return ActionReply::HelperErrorReply(static_cast<int>(ErrorCode::WriteFailed));
        }

        ActionReply reply = ActionReply::SuccessReply();
        // 只回报"写了哪些键 + 备份名"，不回显值（值里可能有内网地址）
        QVariantMap data;
        data.insert(QStringLiteral("backup"), backupPath);
        data.insert(QStringLiteral("keys"), PrivilegedConfigRequest::allowedKeys());
        reply.setData(data);
        return reply;
    }

    /*!
     * 服务管理（B1）：三个固定 unit × 五个固定动词。
     *
     * 每个动词一个槽（动作名 → 槽名的规则见 .actions 里的说明），
     * 但实现共用：**参数校验在白名单函数里**（会话侧与这里都调用它，纵深防御）。
     */
    ActionReply service_start(const QVariantMap &arguments)
    {
        return runServiceAction(ServiceVerb::Start, arguments);
    }
    ActionReply service_stop(const QVariantMap &arguments)
    {
        return runServiceAction(ServiceVerb::Stop, arguments);
    }
    ActionReply service_restart(const QVariantMap &arguments)
    {
        return runServiceAction(ServiceVerb::Restart, arguments);
    }
    ActionReply service_enable(const QVariantMap &arguments)
    {
        return runServiceAction(ServiceVerb::Enable, arguments);
    }
    ActionReply service_disable(const QVariantMap &arguments)
    {
        return runServiceAction(ServiceVerb::Disable, arguments);
    }

    /*! 通过 systemd D-Bus 重启 docker.service（不调用 systemctl 二进制）。

        对应动作 org.kde.kcm.docker.daemon.restart。 */
    ActionReply daemon_restart(const QVariantMap &arguments)
    {
        if (!arguments.isEmpty() && !arguments.contains(QStringLiteral("confirm"))) {
            // 允许一个可选的确认标记，但不接受任何其他参数
            for (auto it = arguments.constBegin(); it != arguments.constEnd(); ++it) {
                if (it.key() != QLatin1String("confirm")) {
                    return ActionReply::HelperErrorReply(static_cast<int>(ErrorCode::InvalidRequest));
                }
            }
        }

        QDBusInterface manager(QStringLiteral("org.freedesktop.systemd1"),
                               QStringLiteral("/org/freedesktop/systemd1"),
                               QStringLiteral("org.freedesktop.systemd1.Manager"),
                               QDBusConnection::systemBus());
        if (!manager.isValid()) {
            return ActionReply::HelperErrorReply(static_cast<int>(ErrorCode::SystemdUnavailable));
        }
        const QDBusReply<QDBusObjectPath> reply = manager.call(QStringLiteral("RestartUnit"),
                                                              QString::fromLatin1(kDockerUnit),
                                                              QStringLiteral("replace"));
        if (!reply.isValid()) {
            qCWarning(kontainerModel) << "helper could not restart docker.service:" << reply.error().message();
            return ActionReply::HelperErrorReply(static_cast<int>(ErrorCode::RestartFailed));
        }
        return ActionReply::SuccessReply();
    }

private:
    /*! 错误码（与客户端约定的稳定值，客户端据此选文案）。 */
    enum class ErrorCode {
        InvalidRequest = 1,
        UnparsableConfig = 2,
        WriteFailed = 3,
        SystemdUnavailable = 4,
        RestartFailed = 5,
    };
};

ActionReply KontainerHelper::runServiceAction(ServiceVerb verb, const QVariantMap &arguments)
{
    // 纵深防御：会话侧已经校验过一次，这里**再校验一次**——即使有人绕过会话侧
    // 直接调用 D-Bus 动作，也只能操作白名单里的三个 unit 与五个固定动词。
    const QString unit = arguments.value(QStringLiteral("unit")).toString();
    const QString errorKey = serviceControlArgumentError(unit, serviceVerbKey(verb));
    if (!errorKey.isEmpty()) {
        qCWarning(kontainerModel) << "service action rejected:" << errorKey << unit;
        return ActionReply::HelperErrorReply(static_cast<int>(ErrorCode::InvalidRequest));
    }

    QDBusInterface manager(QStringLiteral("org.freedesktop.systemd1"),
                           QStringLiteral("/org/freedesktop/systemd1"),
                           QStringLiteral("org.freedesktop.systemd1.Manager"),
                           QDBusConnection::systemBus());
    if (!manager.isValid()) {
        return ActionReply::HelperErrorReply(static_cast<int>(ErrorCode::SystemdUnavailable));
    }

    if (verb == ServiceVerb::Enable || verb == ServiceVerb::Disable) {
        // Enable/Disable 的返回类型与 Start/Stop 不同，单独走消息调用
        const bool enable = verb == ServiceVerb::Enable;
        const QDBusMessage reply = manager.call(enable ? QStringLiteral("EnableUnitFiles") : QStringLiteral("DisableUnitFiles"),
                                                QStringList {unit},
                                                false, // runtime=false：写盘（持久）
                                                true); // force
        if (reply.type() == QDBusMessage::ErrorMessage) {
            qCWarning(kontainerModel) << "helper could not" << serviceVerbKey(verb) << unit << reply.errorMessage();
            return ActionReply::HelperErrorReply(static_cast<int>(ErrorCode::RestartFailed));
        }
    } else {
        const QString method = verb == ServiceVerb::Start    ? QStringLiteral("StartUnit")
            : verb == ServiceVerb::Stop                     ? QStringLiteral("StopUnit")
                                                            : QStringLiteral("RestartUnit");
        const QDBusReply<QDBusObjectPath> reply = manager.call(method, unit, QStringLiteral("replace"));
        if (!reply.isValid()) {
            qCWarning(kontainerModel) << "helper could not" << serviceVerbKey(verb) << unit << reply.error().message();
            return ActionReply::HelperErrorReply(static_cast<int>(ErrorCode::RestartFailed));
        }
    }

    ActionReply ok = ActionReply::SuccessReply();
    QVariantMap data;
    data.insert(QStringLiteral("unit"), unit);
    data.insert(QStringLiteral("verb"), serviceVerbKey(verb));
    ok.setData(data);
    return ok;
}

} // namespace Kontainer

// helper id 取自单一来源常量：它与会话侧的 setHelperId()、.actions 的动作名前缀、
// D-Bus 系统策略的 allow own 必须是同一个字符串
KAUTH_HELPER_MAIN(Kontainer::kHelperId, Kontainer::KontainerHelper)

#include "kcm_docker_helper.moc"
