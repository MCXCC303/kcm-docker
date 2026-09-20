/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Restricted privileged helper (ARCH_V5_V8 §2.4).

    It does exactly two things, always on fixed paths:

      1. `org.kde.kcm.docker.daemon.save`: merge whitelisted-key edits into
         `/etc/docker/daemon.json` (the helper does the reading itself; callers supply no content)
      2. `org.kde.kcm.docker.daemon.restart`: restart `docker.service` over the **systemd D-Bus API**

    The slot names are not arbitrary: KAuth looks a slot up by "action name minus the helper id
    prefix, `.` replaced by `_`" (see KAuth's DBusHelperProxy), so the two actions above map to
    daemon_save / daemon_restart. A wrong name still compiles and only shows up on a real machine as
    "no such action".

    Deliberately not provided (each would be a privilege-escalation backdoor):
      - no path arguments (paths are compile-time constants)
      - no arbitrary JSON or keys
      - no external commands (restart goes over D-Bus, not the `systemctl` binary)
      - no generic "run any command" interface
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
/*! Fixed path: the helper touches this one file only. */
constexpr auto kDaemonConfigPath = "/etc/docker/daemon.json";
/*! Fixed unit name: the helper restarts this one unit only. */
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
     * Shared implementation of the five service actions (defined further down, private section).
     *
     * It takes only the `unit` argument, which must hit the `managedServiceUnits()` whitelist, so
     * even a direct D-Bus call that bypasses the session side cannot reach anything else.
     */
    ActionReply runServiceAction(ServiceVerb verb, const QVariantMap &arguments);

public Q_SLOTS:
    /*! Write daemon.json (edit intents for whitelisted keys). Action ...daemon.save. */
    ActionReply daemon_save(const QVariantMap &arguments)
    {
        PrivilegedConfigRequest request;
        QString errorKey;
        if (!PrivilegedConfigRequest::fromArguments(arguments, &request, &errorKey)) {
            qCWarning(kontainerModel) << "privileged config request rejected:" << errorKey;
            return ActionReply::HelperErrorReply(static_cast<int>(ErrorCode::InvalidRequest));
        }

        if (request.dryRun()) {
            // The "unlock" path: authorize and validate only, never write
            return ActionReply::SuccessReply();
        }

        const QByteArray existing = readConfigFile();
        const QByteArray merged = request.mergeInto(existing);
        if (merged.isEmpty()) {
            // Unparsable existing file: refuse to write instead of overwriting (same rule as the UI)
            return ActionReply::HelperErrorReply(static_cast<int>(ErrorCode::UnparsableConfig));
        }

        QString backupPath;
        const QString error = DaemonConfigWriter::writeAtomically(QString::fromLatin1(kDaemonConfigPath), merged, &backupPath);
        if (!error.isEmpty()) {
            qCWarning(kontainerModel) << "helper failed to write daemon config:" << error;
            return ActionReply::HelperErrorReply(static_cast<int>(ErrorCode::WriteFailed));
        }

        ActionReply reply = ActionReply::SuccessReply();
        // Report only the keys written and the backup name, never values (they may hold LAN addresses)
        QVariantMap data;
        data.insert(QStringLiteral("backup"), backupPath);
        data.insert(QStringLiteral("keys"), PrivilegedConfigRequest::allowedKeys());
        reply.setData(data);
        return reply;
    }

    /*!
     * Service management (B1): three fixed units × five fixed verbs.
     *
     * One slot per verb (the action-name → slot-name rule is documented in .actions) over a shared
     * implementation: **argument validation lives in the whitelist function**, which both the
     * session side and this file call (defense in depth).
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

    /*! Restart docker.service over systemd D-Bus (never the systemctl binary).

        Action org.kde.kcm.docker.daemon.restart. */
    ActionReply daemon_restart(const QVariantMap &arguments)
    {
        if (!arguments.isEmpty() && !arguments.contains(QStringLiteral("confirm"))) {
            // One optional confirmation flag is allowed, nothing else
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
    /*! Error codes (stable values agreed with the client, which picks the message from them). */
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
    // Defense in depth: the session side already validated, and we validate **again** — a direct
    // D-Bus call can still only reach the three whitelisted units and the five fixed verbs.
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
        // Enable/Disable return a different type than Start/Stop, so they use a message call
        const bool enable = verb == ServiceVerb::Enable;
        const QDBusMessage reply = manager.call(enable ? QStringLiteral("EnableUnitFiles") : QStringLiteral("DisableUnitFiles"),
                                                QStringList {unit},
                                                false, // runtime=false: write to disk (persistent)
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

// The helper id comes from the single-source constant: it must equal the session side's
// setHelperId(), the action prefix in .actions and the D-Bus system policy's allow own
KAUTH_HELPER_MAIN(Kontainer::kHelperId, Kontainer::KontainerHelper)

#include "kcm_docker_helper.moc"
