/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/daemon_config_controller.h"

#include "backend/service_control.h"

#include "kauth/privileged_config_request.h"

#include <algorithm>

#include "backend/privileged_client.h"

#include <QFileInfo>
#include <QTimer>
#include "logging.h"

namespace Kontainer
{

namespace
{
/*! `lastError` may hold a technical reason or this key (the UI takes the fallback path on it). */
constexpr auto kPrivilegeRequired = "privilegeRequired";
/*! No privilege path (helper / policy not installed): the UI then hands out a copyable command. */
constexpr auto kHelperUnavailable = "helperUnavailable";
/*! Not unlocked (protected scope): the save is rejected. */
constexpr auto kLocked = "locked";
/*!
 * Unlock lifetime in seconds.
 *
 * Must match the polkit keep window: `auth_admin_keep` remembers for 5 minutes by default and the UI
 * countdown only displays that window; on expiry we lock deliberately, so "continue or not" becomes an
 * explicit user action again instead of a failure at save time.
 */
constexpr int kUnlockKeepSeconds = 300;

/*!
 * Whether two deployment snapshots are equivalent (so the UI is notified only on a real change).
 *
 * Field-by-field on purpose instead of adding operator== to DaemonDeployment: that struct serves the UI,
 * and a comparison operator would hide which fields affect it.
 */
bool sameDeployment(const DaemonDeployment &a, const DaemonDeployment &b)
{
    return a.form == b.form && a.systemConfigPath == b.systemConfigPath && a.userConfigPath == b.userConfigPath
        && a.configPath == b.configPath && a.configExists == b.configExists && a.configWritable == b.configWritable
        && a.configSize == b.configSize && a.configModified == b.configModified
        && a.dataRootInHomeDir == b.dataRootInHomeDir;
}
} // namespace

DaemonConfigController::DaemonConfigController(QObject *parent)
    : QObject(parent)
    , m_unlockTimer(new QTimer(this))
{
    m_unlockTimer->setInterval(1000);
    connect(m_unlockTimer, &QTimer::timeout, this, [this] {
        if (!m_unlocked) {
            return;
        }
        if (m_unlockSecondsRemaining <= 1) {
            lock();
            return;
        }
        --m_unlockSecondsRemaining;
        Q_EMIT authorizationChanged();
    });
    refreshFromDisk();
}

void DaemonConfigController::setScope(const QString &scope)
{
    const QString normalized = scope == QLatin1String("user") ? QStringLiteral("user") : QStringLiteral("system");
    if (m_scope == normalized) {
        return;
    }
    m_scope = normalized;
    lock(); // Switching scope requires re-authorization: the grant is for that one file
    refreshFromDisk();
}

QString DaemonConfigController::scope() const
{
    return m_scope;
}

bool DaemonConfigController::activeScope() const
{
    return m_activeScope;
}

bool DaemonConfigController::unlocked() const
{
    return m_unlocked;
}

int DaemonConfigController::unlockSecondsRemaining() const
{
    return m_unlocked ? m_unlockSecondsRemaining : 0;
}

bool DaemonConfigController::privilegeAvailable() const
{
    return m_privilegedClient && m_privilegedClient->writeAvailable();
}

void DaemonConfigController::requestUnlock()
{
    if (!m_privilegedClient) {
        setLastError(QString::fromLatin1(kHelperUnavailable));
        return;
    }
    setLastError(QString());
    m_awaitingAuthorize = true;
    m_privilegedClient->requestAuthorization();
}

void DaemonConfigController::lock()
{
    m_unlockTimer->stop();
    m_unlockSecondsRemaining = 0;
    if (!m_unlocked) {
        return;
    }
    m_unlocked = false;
    // Late authorization results are not accepted after locking: the user no longer wants the grant
    m_awaitingAuthorize = false;
    Q_EMIT authorizationChanged();
}

void DaemonConfigController::setPrivilegedClient(PrivilegedClient *client)
{
    m_privilegedClient = client;
    if (!client) {
        return;
    }
    connect(client, &PrivilegedClient::finished, this, [this](PrivilegedClient::Operation operation, bool success, const QString &errorKey) {
        if (operation == PrivilegedClient::Operation::ServiceControl) {
            if (!m_serviceInFlight) {
                return; // Service operation started by another page
            }
            m_serviceInFlight = false;
            m_serviceErrorKey = success ? QString() : (errorKey.isEmpty() ? QStringLiteral("serviceControlFailed") : errorKey);
            Q_EMIT changed();
            Q_EMIT serviceControlled(m_serviceUnit, m_serviceVerb, success, m_serviceErrorKey);
            return;
        }
        if (operation == PrivilegedClient::Operation::Authorize) {
            if (!m_awaitingAuthorize) {
                // Authorization from another page: not mine (the shared client broadcasts results)
                return;
            }
            m_awaitingAuthorize = false;
            if (!success) {
                // Cancelling is a normal outcome: stay locked and show no error banner (avoid noise)
                if (errorKey != QLatin1String("cancelled")) {
                    setLastError(errorKey);
                }
                return;
            }
            m_unlocked = true;
            m_unlockSecondsRemaining = kUnlockKeepSeconds;
            m_unlockTimer->start();
            setLastError(QString());
            Q_EMIT authorizationChanged();
            return;
        }
        if (operation == PrivilegedClient::Operation::WriteConfig) {
            if (!m_awaitingWrite) {
                return; // Write started by another page
            }
            m_awaitingWrite = false;
            if (!success) {
                setLastError(errorKey);
                return;
            }
            refreshFromDisk();
            Q_EMIT resultChanged();
            Q_EMIT saved();
            return;
        }
        if (!m_awaitingRestart) {
            return; // Restart started by another page
        }
        m_awaitingRestart = false;
        Q_EMIT restarted(success, errorKey);
    });
}

void DaemonConfigController::setRunningContainerCount(int count)
{
    if (m_runningContainers == count) {
        return;
    }
    m_runningContainers = count;
    Q_EMIT changed();
}

int DaemonConfigController::runningContainers() const
{
    return m_runningContainers;
}

bool DaemonConfigController::controlService(const QString &unit, const QString &verbKey)
{
    // Whitelist check: invalid requests are rejected right here and start **no** privileged action
    const QString argumentError = serviceControlArgumentError(unit, verbKey);
    if (!argumentError.isEmpty()) {
        m_serviceErrorKey = argumentError;
        m_serviceUnit = unit;
        m_serviceVerb = verbKey;
        Q_EMIT changed();
        return false;
    }
    if (!m_privilegedClient) {
        m_serviceErrorKey = QStringLiteral("helperUnavailable");
        m_serviceUnit = unit;
        m_serviceVerb = verbKey;
        Q_EMIT changed();
        return false;
    }
    m_serviceUnit = unit;
    m_serviceVerb = verbKey;
    m_serviceErrorKey.clear();
    m_serviceInFlight = true;
    Q_EMIT changed();
    m_privilegedClient->controlService(unit, verbKey);
    return true;
}

void DaemonConfigController::restartDocker()
{
    if (!m_privilegedClient) {
        Q_EMIT restarted(false, QStringLiteral("helperUnavailable"));
        return;
    }
    m_awaitingRestart = true;
    m_privilegedClient->restartDocker(m_deployment.form != DaemonForm::Rootless);
}

void DaemonConfigController::setEngineInfo(const EngineInfo &info)
{
    m_engine = info;

    // Re-reading from disk is **not** allowed here: `setEngineInfo()` is called repeatedly by status
    // refreshes (auto refresh, container list updates), and re-reading would clear `m_edits` and dirty --
    // while editing mirrors or concurrent downloads the UI would snap back to the old disk values
    // (reported bug). Re-reading happens via explicit reload(), after save, or on a scope switch.
    const bool mirrorsChanged = m_activeMirrors != info.registryMirrors;
    m_activeMirrors = info.registryMirrors;

    if (refreshDeployment() || mirrorsChanged) {
        Q_EMIT changed();
    }
}

QString DaemonConfigController::formKey() const
{
    return m_deployment.formKey();
}

QString DaemonConfigController::configPath() const
{
    return m_deployment.configPath;
}

bool DaemonConfigController::configExists() const
{
    return m_deployment.configExists;
}

bool DaemonConfigController::configWritable() const
{
    return m_deployment.configWritable;
}

bool DaemonConfigController::requiresPrivilege() const
{
    // Only whether the current scope's file is writable (form detection lives in DaemonDeployment)
    return m_deployment.requiresPrivilege();
}

bool DaemonConfigController::dataRootInHomeDir() const
{
    return m_deployment.dataRootInHomeDir;
}

QString DaemonConfigController::parseError() const
{
    return m_document.isValid() ? QString() : m_document.errorText();
}

QStringList DaemonConfigController::registryMirrors() const
{
    return m_edits.setRegistryMirrors ? m_edits.registryMirrors : m_document.registryMirrors();
}

QStringList DaemonConfigController::insecureRegistries() const
{
    return m_edits.setInsecureRegistries ? m_edits.insecureRegistries : m_document.insecureRegistries();
}

int DaemonConfigController::maxConcurrentDownloads() const
{
    // 0 means "use the daemon default" (shown as "Default"): it also covers removing the key
    if (m_edits.concurrentDownloadsEdit == ConfigEdit::Set) {
        return m_edits.maxConcurrentDownloads;
    }
    if (m_edits.concurrentDownloadsEdit == ConfigEdit::Remove) {
        return 0;
    }
    return m_document.maxConcurrentDownloads();
}

QString DaemonConfigController::logDriver() const
{
    if (m_edits.logDriverEdit == ConfigEdit::Set) {
        return m_edits.logDriver;
    }
    if (m_edits.logDriverEdit == ConfigEdit::Remove) {
        return QString();
    }
    return m_document.logDriver();
}

QString DaemonConfigController::dataRoot() const
{
    return m_document.dataRoot();
}

QString DaemonConfigController::configuredStorageDriver() const
{
    return m_document.storageDriver();
}

QStringList DaemonConfigController::unmanagedKeys() const
{
    return m_document.unmanagedKeys();
}

QStringList DaemonConfigController::activeRegistryMirrors() const
{
    return m_activeMirrors;
}

bool DaemonConfigController::restartPending() const
{
    // Restart pending = the configured mirrors differ from the mirrors `/info` reports.
    // Docker normalizes mirrors (trailing slash, order), so compare sets rather than string by string.
    QStringList configured = registryMirrors();
    QStringList active = m_activeMirrors;
    configured.sort();
    active.sort();
    return configured != active;
}

bool DaemonConfigController::liveRestoreEnabled() const
{
    return m_engine.liveRestoreEnabled;
}

QStringList DaemonConfigController::backups() const
{
    return m_backups;
}

QString DaemonConfigController::lastError() const
{
    return m_lastError;
}

QString DaemonConfigController::lastBackupPath() const
{
    return m_lastBackupPath;
}

bool DaemonConfigController::dirty() const
{
    return m_dirty;
}

void DaemonConfigController::reload()
{
    refreshFromDisk();
}

bool DaemonConfigController::refreshDeployment()
{
    const DaemonDeployment previous = m_deployment;
    const bool previousActiveScope = m_activeScope;

    m_deployment = DaemonDeploymentDetector::detect(m_engine);

    // The scope picks which file is read: user ~/.config/docker/daemon.json, system /etc/docker/daemon.json.
    // That is a different question from "which daemon reads it" -- activeScope tells the UI the latter
    // (otherwise edits silently do not take effect).
    const QString scopedPath = m_scope == QLatin1String("user") ? m_deployment.userConfigPath : m_deployment.systemConfigPath;
    m_deployment.configPath = scopedPath;
    const QFileInfo scopedInfo(scopedPath);
    m_deployment.configExists = scopedInfo.exists();
    m_deployment.configWritable = DaemonDeploymentDetector::configIsWritable(scopedPath);
    m_deployment.configSize = m_deployment.configExists ? scopedInfo.size() : 0;
    m_deployment.configModified = m_deployment.configExists ? scopedInfo.lastModified() : QDateTime();

    const bool rootlessDaemon = m_deployment.form == DaemonForm::Rootless;
    m_activeScope = (m_scope == QLatin1String("user")) == rootlessDaemon;

    // Periodic refreshes (every engine/container list update) reach here, so notify the UI only on a
    // **real** change: it avoids pointless binding recomputation and list rebuilds (refresh jitter) and
    // does not disturb what the user is editing
    return previousActiveScope != m_activeScope || !sameDeployment(previous, m_deployment);
}

void DaemonConfigController::refreshFromDisk()
{
    refreshDeployment();

    m_document = DaemonConfigDocument::fromFile(m_deployment.configPath);
    m_backups = DaemonConfigWriter::listBackups(m_deployment.configPath);
    m_unlockSecondsRemaining = m_unlocked ? m_unlockSecondsRemaining : 0;
    // After re-reading, edit state resets (the disk values are the new baseline)
    m_edits = DaemonConfigEdits();
    setDirty(false);
    Q_EMIT changed();
}

void DaemonConfigController::setRegistryMirrors(const QStringList &mirrors)
{
    m_edits.setRegistryMirrors = true;
    m_edits.registryMirrors = mirrors;
    setDirty(true);
    Q_EMIT changed();
}

void DaemonConfigController::setInsecureRegistries(const QStringList &registries)
{
    m_edits.setInsecureRegistries = true;
    m_edits.insecureRegistries = registries;
    setDirty(true);
    Q_EMIT changed();
}

void DaemonConfigController::setMaxConcurrentDownloads(int value)
{
    // 0 = back to the default (remove the key), > 0 = write it
    m_edits.concurrentDownloadsEdit = value > 0 ? ConfigEdit::Set : ConfigEdit::Remove;
    m_edits.maxConcurrentDownloads = std::max(0, value);
    setDirty(true);
    Q_EMIT changed();
}

void DaemonConfigController::setLogDriver(const QString &driver)
{
    // Empty string = back to the default (remove the key)
    m_edits.logDriverEdit = driver.isEmpty() ? ConfigEdit::Remove : ConfigEdit::Set;
    m_edits.logDriver = driver;
    setDirty(true);
    Q_EMIT changed();
}

DaemonConfigEdits DaemonConfigController::buildEdits() const
{
    return m_edits;
}

QString DaemonConfigController::pendingContentPreview() const
{
    const QByteArray merged = m_document.merged(buildEdits());
    return QString::fromUtf8(merged);
}

bool DaemonConfigController::save()
{
    setLastError(QString());

    if (!m_document.isValid()) {
        setLastError(QStringLiteral("configUnparsable"));
        return false;
    }
    if (m_edits.isEmpty()) {
        setLastError(QStringLiteral("nothingToSave"));
        return false;
    }

    const QByteArray merged = m_document.merged(buildEdits());
    if (merged.isEmpty()) {
        setLastError(QStringLiteral("mergeFailed"));
        return false;
    }

    if (requiresPrivilege()) {
        // Protected scope: must be unlocked (the UI also disables Save while locked; this is the backstop)
        if (!m_unlocked) {
            setLastError(QString::fromLatin1(kLocked));
            return false;
        }
        // Hand it to the restricted helper. With no privilege path, say so plainly (the UI falls back to
        // the copyable command); never let the user believe "clicked Save = saved"
        if (!m_privilegedClient) {
            setLastError(QString::fromLatin1(kHelperUnavailable));
            return false;
        }
        m_awaitingWrite = true;
        m_privilegedClient->writeConfig(buildEdits());
        return false; // The result returns asynchronously via finished() (saved() on success)
    }

    QString backupPath;
    const QString error = DaemonConfigWriter::writeAtomically(m_deployment.configPath, merged, &backupPath);
    if (!error.isEmpty()) {
        qCWarning(kontainerModel) << "writing daemon config failed:" << error;
        setLastError(QStringLiteral("writeFailed"));
        return false;
    }

    m_lastBackupPath = backupPath;
    refreshFromDisk();
    Q_EMIT resultChanged();
    Q_EMIT saved();
    return true;
}

bool DaemonConfigController::restoreBackup(const QString &backupPath)
{
    setLastError(QString());

    QString path = backupPath;
    if (path.isEmpty()) {
        const QStringList available = DaemonConfigWriter::listBackups(m_deployment.configPath);
        if (available.isEmpty()) {
            setLastError(QStringLiteral("noBackup"));
            return false;
        }
        path = available.first();
    }

    const QByteArray content = DaemonConfigWriter::readBackup(path);
    if (content.isEmpty()) {
        setLastError(QStringLiteral("backupUnreadable"));
        return false;
    }
    if (requiresPrivilege()) {
        // Restoring also writes a system file: it must go through the helper too (no bypass here)
        setLastError(m_privilegedClient ? QString::fromLatin1(kPrivilegeRequired) : QString::fromLatin1(kHelperUnavailable));
        return false;
    }

    QString newBackup;
    const QString error = DaemonConfigWriter::writeAtomically(m_deployment.configPath, content, &newBackup);
    if (!error.isEmpty()) {
        qCWarning(kontainerModel) << "restoring daemon config failed:" << error;
        setLastError(QStringLiteral("writeFailed"));
        return false;
    }
    m_lastBackupPath = newBackup;
    refreshFromDisk();
    Q_EMIT resultChanged();
    Q_EMIT saved();
    return true;
}

QStringList DaemonConfigController::selectableLogDrivers() const
{
    QStringList drivers;
    drivers.append(QString()); // "Default": remove the log-driver key
    drivers.append(PrivilegedConfigRequest::allowedLogDrivers());
    return drivers;
}

QString DaemonConfigController::privilegedCommand() const
{
    const QByteArray merged = m_document.merged(buildEdits());
    const QString path = m_deployment.configPath.isEmpty() ? QStringLiteral("/etc/docker/daemon.json") : m_deployment.configPath;
    // tee + here-doc: the user copies it into a terminal and the content matches the UI preview.
    // Note: this generates no "arbitrary command" form such as `sudo sh -c`, only a restricted write.
    // The restart step depends on the daemon form: a rootless daemon is the user's own service, so
    // `systemctl --user` suffices -- sudo against a system service is neither needed nor right.
    const QString restart = m_deployment.form == DaemonForm::Rootless
        ? QStringLiteral("systemctl --user restart docker")
        : QStringLiteral("sudo systemctl restart docker");
    return QStringLiteral("sudo tee %1 >/dev/null <<'EOF'\n%2EOF\n%3").arg(path, QString::fromUtf8(merged), restart);
}

void DaemonConfigController::setLastError(const QString &error)
{
    if (m_lastError == error) {
        return;
    }
    m_lastError = error;
    Q_EMIT resultChanged();
}

void DaemonConfigController::setDirty(bool dirty)
{
    if (m_dirty == dirty) {
        return;
    }
    m_dirty = dirty;
    Q_EMIT dirtyChanged();
}

} // namespace Kontainer
