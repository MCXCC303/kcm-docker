/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/daemon_config.h"
#include "backend/daemon_deployment.h"
#include "domain/engine_info.h"

#include <QObject>
#include <QStringList>

class QTimer;

namespace Kontainer
{

class PrivilegedClient;

/*!
 * Controller for the runtime configuration page (ARCH_V5_V8 §2.2/§2.3/§2.4).
 *
 * Duties:
 *  - probe the deployment form and config file state (read-only)
 *  - read `daemon.json` (unknown keys allowed) and expose whitelisted fields to the UI
 *  - compare against the real `/info` values and mark "in effect / restart pending"
 *  - save (atomic write on user-writable paths; system paths go through the 5C privileged helper)
 *  - generate "do it yourself" fallback commands (a missing helper must not dead-end the feature)
 *
 * Not responsible for: QML copy (the UI owns it), privilege details (the helper owns them).
 */
class DaemonConfigController : public QObject
{
    Q_OBJECT

    /* --- Scope (ARCH_V5_V8 §2.2 fix: split by "which daemon reads this file") --- */
    /*! `user` (~/.config/docker/daemon.json) or `system` (/etc/docker/daemon.json). */
    /*!
     * Service control (B1): invalid requests are rejected with a stable key; the UI disables buttons while
     * one is in flight. These must be declared as Q_PROPERTY -- otherwise QML reads undefined forever
     * (hit once: the value was right yet QML logged "Unable to assign [undefined] to bool").
     */
    Q_PROPERTY(QString serviceErrorKey READ serviceErrorKey NOTIFY changed)
    Q_PROPERTY(QString serviceUnit READ serviceUnit NOTIFY changed)
    Q_PROPERTY(QString serviceVerb READ serviceVerb NOTIFY changed)
    Q_PROPERTY(bool serviceInFlight READ serviceInFlight NOTIFY changed)

    Q_PROPERTY(QString scope READ scope NOTIFY changed)
    /*! Whether this scope is the one the running daemon reads. */
    Q_PROPERTY(bool activeScope READ activeScope NOTIFY changed)

    /* --- Unlock state (protected scope) --- */
    /*! Whether polkit authorization succeeded (meaningful for protected scopes only). */
    Q_PROPERTY(bool unlocked READ unlocked NOTIFY authorizationChanged)
    /*! Seconds of authorization left; 0 means locked. */
    Q_PROPERTY(int unlockSecondsRemaining READ unlockSecondsRemaining NOTIFY authorizationChanged)
    /*! Whether a privilege path exists here (helper/policy installed). */
    Q_PROPERTY(bool privilegeAvailable READ privilegeAvailable NOTIFY changed)

    /* --- Deployment and file state --- */
    Q_PROPERTY(QString formKey READ formKey NOTIFY changed)
    Q_PROPERTY(QString configPath READ configPath NOTIFY changed)
    Q_PROPERTY(bool configExists READ configExists NOTIFY changed)
    Q_PROPERTY(bool configWritable READ configWritable NOTIFY changed)
    Q_PROPERTY(bool requiresPrivilege READ requiresPrivilege NOTIFY changed)
    Q_PROPERTY(bool dataRootInHomeDir READ dataRootInHomeDir NOTIFY changed)
    /*! Technical reason for a parse failure; empty means the file is usable (or absent). */
    Q_PROPERTY(QString parseError READ parseError NOTIFY changed)

    /* --- Settings we manage --- */
    Q_PROPERTY(QStringList registryMirrors READ registryMirrors NOTIFY changed)
    Q_PROPERTY(QStringList insecureRegistries READ insecureRegistries NOTIFY changed)
    Q_PROPERTY(int maxConcurrentDownloads READ maxConcurrentDownloads NOTIFY changed)
    Q_PROPERTY(QString logDriver READ logDriver NOTIFY changed)
    /*! `data-root` / `storage-driver` (read-only display). */
    Q_PROPERTY(QString dataRoot READ dataRoot NOTIFY changed)
    Q_PROPERTY(QString configuredStorageDriver READ configuredStorageDriver NOTIFY changed)
    /*! Keys we do not manage (the UI shows "Other keys: N (read-only)"). */
    Q_PROPERTY(QStringList unmanagedKeys READ unmanagedKeys NOTIFY changed)

    /* --- In-effect state --- */
    /*! Registry mirrors reported by `/info` (the values actually in effect). */
    Q_PROPERTY(QStringList activeRegistryMirrors READ activeRegistryMirrors NOTIFY changed)
    /*! Saved but not yet in effect (a daemon restart is needed). */
    Q_PROPERTY(bool restartPending READ restartPending NOTIFY changed)
    /*! `LiveRestoreEnabled`: when false, a restart stops running containers. */
    Q_PROPERTY(bool liveRestoreEnabled READ liveRestoreEnabled NOTIFY changed)
    /*! Backup list for the current config file (newest first). */
    Q_PROPERTY(QStringList backups READ backups NOTIFY changed)

    /* --- Save results --- */
    Q_PROPERTY(QString lastError READ lastError NOTIFY resultChanged)
    Q_PROPERTY(QString lastBackupPath READ lastBackupPath NOTIFY resultChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)

public:
    /*! Default scope follows the running daemon (rootless -> user, system-wide -> system). */
    explicit DaemonConfigController(QObject *parent = nullptr);

    /*! Update the "in effect" baseline when engine info arrives (`/info`). */
    void setEngineInfo(const EngineInfo &info);

    /*!
     * Inject the privileged client (the composition root does this; null means no privilege path here).
     * When null, a save needing privileges hands out the fallback command instead of failing silently.
     */
    void setPrivilegedClient(PrivilegedClient *client);
    /*! Running container count (for restart impact hints), provided by StatusController. */
    void setRunningContainerCount(int count);

    QString formKey() const;
    QString configPath() const;
    bool configExists() const;
    bool configWritable() const;
    bool requiresPrivilege() const;
    bool dataRootInHomeDir() const;
    QString parseError() const;

    QStringList registryMirrors() const;
    QStringList insecureRegistries() const;
    int maxConcurrentDownloads() const;
    QString logDriver() const;
    QString dataRoot() const;
    QString configuredStorageDriver() const;
    QStringList unmanagedKeys() const;

    QStringList activeRegistryMirrors() const;
    bool restartPending() const;
    bool liveRestoreEnabled() const;
    QStringList backups() const;

    QString lastError() const;
    QString lastBackupPath() const;
    bool dirty() const;
    /*! Running container count (used in the restart confirmation wording). */
    int runningContainers() const;

    /*! Re-probe and re-read the file (on page entry and after save/restart). */
    Q_INVOKABLE void reload();

    /*! Switch scope (the UI splits the scopes into two pages/entries). */
    Q_INVOKABLE void setScope(const QString &scope);
    QString serviceErrorKey() const
    {
        return m_serviceErrorKey;
    }
    QString serviceUnit() const
    {
        return m_serviceUnit;
    }
    QString serviceVerb() const
    {
        return m_serviceVerb;
    }
    bool serviceInFlight() const
    {
        return m_serviceInFlight;
    }

    QString scope() const;
    bool activeScope() const;

    /*! "Unlock": authorize the write-config action once (keep remembers per action, so the following
     * save and restart do not prompt again). */
    Q_INVOKABLE void requestUnlock();
    /*! Lock manually (the user gives the privileges up). */
    Q_INVOKABLE void lock();
    bool unlocked() const;
    int unlockSecondsRemaining() const;
    bool privilegeAvailable() const;

    /* --- Edits (the UI pushes current values back; untouched fields mean "do not change") --- */
    Q_INVOKABLE void setRegistryMirrors(const QStringList &mirrors);
    Q_INVOKABLE void setInsecureRegistries(const QStringList &registries);
    Q_INVOKABLE void setMaxConcurrentDownloads(int value);
    Q_INVOKABLE void setLogDriver(const QString &driver);

    /*! Merge the current edits into the document and return a preview (shown in the confirm dialog). */
    Q_INVOKABLE QString pendingContentPreview() const;

    /*!
     * Save.
     *
     * User-writable path -> atomic write and true;
     * privilege needed -> false and `lastError` = `privilegeRequired` (5C hands it to the helper).
     */
    Q_INVOKABLE bool save();

    /*! Restore a backup (pass the backup file path; an empty string means the most recent one). */
    Q_INVOKABLE bool restoreBackup(const QString &backupPath = QString());

    /*! "Do it yourself" command for when the helper is unavailable (paste it straight into a terminal). */
    /*!
     * Selectable log drivers (first entry is an empty string = use the daemon default).
     *
     * The list comes from the helper whitelist (`PrivilegedConfigRequest::allowedLogDrivers`); the UI must
     * not keep a copy of its own -- the outcome is a UI that offers what the helper rejects.
     */
    Q_INVOKABLE QStringList selectableLogDrivers() const;

    Q_INVOKABLE QString privilegedCommand() const;

    /*! Restart Docker (system-wide goes through the helper, rootless through the session systemd). */
    /*!
     * Control a Docker-related service (B1): the unit must be whitelisted and verbKey one of five fixed
     * verbs. Invalid requests are rejected right here (stable key, no privileged action is sent).
     */
    Q_INVOKABLE bool controlService(const QString &unit, const QString &verbKey);
    Q_INVOKABLE void restartDocker();
    /*! Running container count: the restart confirmation needs it ("will stop N running containers"). */
    Q_PROPERTY(int runningContainers READ runningContainers NOTIFY changed)

Q_SIGNALS:
    void changed();
    /*! Unlock state or remaining time changed. */
    void authorizationChanged();
    void resultChanged();
    void dirtyChanged();
    /*! Save succeeded (the page then shows "restart pending" or "written"). */
    void saved();
    /*! Service control finished, successfully or not; the UI refreshes state and hints. */
    void serviceControlled(const QString &unit, const QString &verb, bool success, const QString &errorKey);
    /*! Restart result (the page hints accordingly; on success the daemon is briefly unavailable). */
    void restarted(bool success, const QString &errorKey);

private:
    /*! Re-read from disk (explicit action): disk values become the baseline and pending edits are dropped. */
    void refreshFromDisk();
    /*!
     * Recompute only the probed facts (paths, writability, active scope, data-root hint), **never** the
     * document or pending edits; returns whether anything changed.
     *
     * Automatic refresh calls it repeatedly, so it must stay cheap and side-effect free.
     */
    bool refreshDeployment();
    DaemonConfigEdits buildEdits() const;
    void setLastError(const QString &error);
    void setDirty(bool dirty);

    EngineInfo m_engine;
    DaemonDeployment m_deployment;
    DaemonConfigDocument m_document;

    /*! Edit state: only fields touched by a set* call are written back. */
    DaemonConfigEdits m_edits;

    QStringList m_activeMirrors;
    QStringList m_backups;
    QString m_lastError;
    QString m_lastBackupPath;
    bool m_dirty = false;
    PrivilegedClient *m_privilegedClient = nullptr;
    /*
     * The KAuth client is **shared** (DockerKcm creates one; each scope has its own controller) and its
     * finished() is a broadcast that does not identify the requester. Each controller must therefore
     * remember what it sent, or unlocking on the system page marks the user page unlocked too (reported:
     * unlock -> lock -> open user settings, still shown as unlocked).
     */
    bool m_awaitingAuthorize = false;
    bool m_awaitingWrite = false;
    bool m_awaitingRestart = false;
    /*! Service control (B1): the most recent request and its result. */
    QString m_serviceUnit;
    QString m_serviceVerb;
    QString m_serviceErrorKey;
    bool m_serviceInFlight = false;
    int m_runningContainers = 0;
    /*! Current scope (user / system) and whether it is the file the running daemon reads. */
    QString m_scope = QStringLiteral("system");
    bool m_activeScope = true;
    /*! Unlock state: locks itself on expiry (the polkit keep window is about 5 minutes). */
    bool m_unlocked = false;
    int m_unlockSecondsRemaining = 0;
    QTimer *m_unlockTimer = nullptr;
};

} // namespace Kontainer
