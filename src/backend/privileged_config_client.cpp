/*
    SPDX-FileCopyrightText: 2026 kontainer developers
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
 * KAuth 失败 → 界面用的稳定 key（文案在 QML 侧，不在这里拼用户可见文本）。
 *
 * 分三类：用户自己取消的、权限被拒的、以及"根本没有可用 helper"的
 * —— 最后一类必须与"失败"区分开，因为界面要给出"自己动手"的命令而不是重试。
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
        // policy 或 helper 没安装：这不是"操作失败"，而是"这条路不存在" → 界面给降级命令
        return QStringLiteral("helperUnavailable");
    default:
        break;
    }

    // helper 自己返回的错误码（见 kcm_docker_helper.cpp 的 ErrorCode）
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
    // KAuth 的 action 只有在 policy 与 helper 都安装时才"可用"；
    // 这里只做一次便宜的判断，真正的结论以执行结果为准（失败会给 helperUnavailable）
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
    // 只传白名单键的编辑意图：helper 自己读文件、自己合并（调用方给不了任意内容）
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
        // 显式的删除意图：helper 只接受它管理范围内的键名
        arguments.insert(QStringLiteral("remove"), removeKeys);
    }
    runHelperAction(QString::fromLatin1(kSaveActionName), arguments, Operation::WriteConfig);
}

void PrivilegedConfigClient::restartDocker(bool systemService)
{
    if (!systemService) {
        // rootless：重启属于用户自己的服务，不需要提权
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
    // 必须显式声明 helper：polkit 后端的执行路径要求动作带 helper
    // （KAuth 的 Polkit1Backend 声明的是 AuthorizeFromHelperCapability，
    //  ExecuteJob 在"没有 helper"时直接返回 InvalidActionReply，不会去执行任何东西）
    action.setHelperId(QString::fromLatin1(kHelperId));
    action.setArguments(arguments);
    if (!action.isValid()) {
        // policy / helper 未安装：直接走降级路径，而不是弹一个必然失败的授权框
        Q_EMIT finished(operation, false, QStringLiteral("helperUnavailable"));
        return;
    }

    m_inFlight = true;
    KAuth::ExecuteJob *job = action.execute();
    connect(job, &KJob::result, this, [this, job, operation] {
        m_inFlight = false;
        // KJob::error() 才是结果：HelperFailed/UserCancelled/AuthorizationDenied… 都在这里
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
        // 非法请求：不发任何提权动作，直接按失败上报（错误 key 由调用方按同一函数算出来）
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
