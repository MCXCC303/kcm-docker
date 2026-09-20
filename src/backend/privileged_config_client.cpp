/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/privileged_config_client.h"

#include "backend/service_control.h"

#include "backend/privileged_client.h"

#include "kauth/privileged_config_request.h"
#include "logging.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>

#include <KJob>

#include <KAuth/Action>
#include <KAuth/ExecuteJob>

namespace Kontainer
{

namespace
{
constexpr auto kDockerUnit = "docker.service";

/*!
 * KAuth failure -> stable key for the UI (text lives in QML; no user-visible strings are built here).
 *
 * Three groups: user-cancelled, authorization denied, and "no usable helper at all" — the last must
 * stay distinct from plain failure, because the UI then offers a do-it-yourself command, not a retry.
 */
QString errorKeyForJob(KAuth::ExecuteJob *job)
{
    switch (job->error()) {
    case KAuth::ActionReply::UserCancelledError:
        return QStringLiteral("cancelled");
    case KAuth::ActionReply::AuthorizationDeniedError:
        return QStringLiteral("authorizationDenied");
    case KAuth::ActionReply::HelperBusyError:
        return QStringLiteral("helperBusy");
    case KAuth::ActionReply::NoSuchActionError:
    case KAuth::ActionReply::NoResponderError:
    case KAuth::ActionReply::InvalidActionError:
        // policy or helper missing: not a failed operation but a path that does not exist -> fallback command
        return QStringLiteral("helperUnavailable");
    default:
        break;
    }

    // error codes returned by the helper itself (see ErrorCode in kcm_docker_helper.cpp)
    switch (job->data().value(QStringLiteral("errorCode")).toInt()) {
    case 1:
        return QStringLiteral("invalidRequest");
    case 2:
        return QStringLiteral("configUnparsable");
    case 3:
        return QStringLiteral("writeFailed");
    case 4:
        return QStringLiteral("systemdUnavailable");
    case 5:
        return QStringLiteral("restartFailed");
    default:
        break;
    }
    return QStringLiteral("helperUnavailable");
}

} // namespace

PrivilegedConfigClient::PrivilegedConfigClient(QObject *parent)
    : PrivilegedClient(parent)
{
}

bool PrivilegedConfigClient::writeAvailable() const
{
    // A KAuth action is valid only when both policy and helper are installed; this is a cheap
    // check, the real answer comes from execution (failure yields helperUnavailable)
    KAuth::Action action(QString::fromLatin1(kSaveActionName));
    action.setHelperId(QString::fromLatin1(kHelperId));
    return action.isValid();
}

void PrivilegedConfigClient::requestAuthorization()
{
    if (m_inFlight) {
        Q_EMIT finished(Operation::Authorize, false, QStringLiteral("helperBusy"));
        return;
    }
    QVariantMap arguments;
    arguments.insert(QStringLiteral("dryRun"), true);
    runHelperAction(QString::fromLatin1(kSaveActionName), arguments, Operation::Authorize);
}

void PrivilegedConfigClient::writeConfig(const DaemonConfigEdits &edits)
{
    if (m_inFlight) {
        Q_EMIT finished(Operation::WriteConfig, false, QStringLiteral("helperBusy"));
        return;
    }
    QVariantMap arguments;
    // Only whitelisted edit intents: the helper reads and merges the file itself (no arbitrary content)
    if (edits.setRegistryMirrors) {
        arguments.insert(QStringLiteral("registry-mirrors"), edits.registryMirrors);
    }
    if (edits.setInsecureRegistries) {
        arguments.insert(QStringLiteral("insecure-registries"), edits.insecureRegistries);
    }
    QStringList removeKeys;
    switch (edits.concurrentDownloadsEdit) {
    case ConfigEdit::Set:
        arguments.insert(QStringLiteral("max-concurrent-downloads"), edits.maxConcurrentDownloads);
        break;
    case ConfigEdit::Remove:
        removeKeys.append(QStringLiteral("max-concurrent-downloads"));
        break;
    case ConfigEdit::Unchanged:
        break;
    }
    switch (edits.logDriverEdit) {
    case ConfigEdit::Set:
        arguments.insert(QStringLiteral("log-driver"), edits.logDriver);
        break;
    case ConfigEdit::Remove:
        removeKeys.append(QStringLiteral("log-driver"));
        break;
    case ConfigEdit::Unchanged:
        break;
    }
    if (!removeKeys.isEmpty()) {
        // Explicit removal intent: the helper accepts only keys it manages
        arguments.insert(QStringLiteral("remove"), removeKeys);
    }
    runHelperAction(QString::fromLatin1(kSaveActionName), arguments, Operation::WriteConfig);
}

void PrivilegedConfigClient::restartDocker(bool systemService)
{
    if (!systemService) {
        // rootless: restarting the user's own service needs no privilege
        restartViaSessionSystemd();
        return;
    }
    if (m_inFlight) {
        Q_EMIT finished(Operation::Restart, false, QStringLiteral("helperBusy"));
        return;
    }
    QVariantMap arguments;
    arguments.insert(QStringLiteral("confirm"), true);
    runHelperAction(QString::fromLatin1(kRestartActionName), arguments, Operation::Restart);
}

void PrivilegedConfigClient::runHelperAction(const QString &actionName, const QVariantMap &arguments, Operation operation)
{
    KAuth::Action action(actionName);
    // The helper must be set explicitly: the polkit backend's execute path requires it (KAuth's
    // Polkit1Backend advertises AuthorizeFromHelperCapability; with no helper ExecuteJob returns
    // InvalidActionReply and executes nothing)
    action.setHelperId(QString::fromLatin1(kHelperId));
    action.setArguments(arguments);
    if (!action.isValid()) {
        // policy / helper not installed: take the fallback path instead of showing a doomed auth dialog
        Q_EMIT finished(operation, false, QStringLiteral("helperUnavailable"));
        return;
    }

    m_inFlight = true;
    KAuth::ExecuteJob *job = action.execute();
    connect(job, &KJob::result, this, [this, job, operation] {
        m_inFlight = false;
        // KJob::error() is the result: HelperFailed/UserCancelled/AuthorizationDenied all land here
        const bool success = job->error() == KJob::NoError;
        const QString errorKey = success ? QString() : errorKeyForJob(job);
        if (!success) {
            qCWarning(kontainerModel) << "privileged action failed:" << job->error() << job->errorText();
        }
        Q_EMIT finished(operation, success, errorKey);
        job->deleteLater();
    });
    job->start();
}

void PrivilegedConfigClient::controlService(const QString &unit, const QString &verbKey)
{
    ServiceVerb verb = ServiceVerb::Start;
    if (!serviceControlArgumentError(unit, verbKey).isEmpty() || !serviceVerbFromKey(verbKey, &verb)) {
        // Invalid request: no privileged action, report failure (caller derives the key the same way)
        Q_EMIT finished(Operation::ServiceControl, false, serviceControlArgumentError(unit, verbKey));
        return;
    }
    runHelperAction(serviceActionName(verb), {{QStringLiteral("unit"), unit}}, Operation::ServiceControl);
}

void PrivilegedConfigClient::restartViaSessionSystemd()
{
    m_inFlight = true;
    QDBusInterface manager(QStringLiteral("org.freedesktop.systemd1"),
                           QStringLiteral("/org/freedesktop/systemd1"),
                           QStringLiteral("org.freedesktop.systemd1.Manager"),
                           QDBusConnection::sessionBus());
    if (!manager.isValid()) {
        m_inFlight = false;
        Q_EMIT finished(Operation::Restart, false, QStringLiteral("systemdUnavailable"));
        return;
    }
    const QDBusReply<QDBusObjectPath> reply = manager.call(QStringLiteral("RestartUnit"),
                                                          QString::fromLatin1(kDockerUnit),
                                                          QStringLiteral("replace"));
    m_inFlight = false;
    if (!reply.isValid()) {
        qCWarning(kontainerModel) << "session systemd could not restart docker:" << reply.error().message();
        Q_EMIT finished(Operation::Restart, false, QStringLiteral("restartFailed"));
        return;
    }
    Q_EMIT finished(Operation::Restart, true, QString());
}

} // namespace Kontainer
