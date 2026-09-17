/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    受限提权 helper（ARCH_V5_V8 §2.4）。

    它只做两件事，而且只按固定路径做：

      1. `org.kde.kontainer.write_daemon_config`：把白名单键的编辑合并进
         `/etc/docker/daemon.json`（读取由 helper 自己做，调用方给不了任意内容）
      2. `org.kde.kontainer.restart_docker`：通过 **systemd 的 D-Bus 接口**重启 `docker.service`

    刻意不提供的能力（否则就是提权后门）：
      - 不接受路径参数（路径是编译期常量）
      - 不接受任意 JSON / 任意键
      - 不执行任何外部命令（重启走 D-Bus，不用 `systemctl` 二进制）
      - 不做"运行任意命令"的通用接口
*/

#include "backend/daemon_config.h"
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

public Q_SLOTS:
    /*! 写入 daemon.json（白名单键的编辑意图）。 */
    ActionReply write_daemon_config(const QVariantMap &arguments)
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

    /*! 通过 systemd D-Bus 重启 docker.service（不调用 systemctl 二进制）。 */
    ActionReply restart_docker(const QVariantMap &arguments)
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

} // namespace Kontainer

KAUTH_HELPER_MAIN("org.kde.kontainer", Kontainer::KontainerHelper)

#include "kontainer_helper.moc"
