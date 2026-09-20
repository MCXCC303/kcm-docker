/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace Kontainer
{

/*!
 * The helper's D-Bus name / KAuth helper id (**single source**).
 *
 * The same string must appear in four places; any mismatch shows up on a real machine only as
 * "authorization failed", with no hint of which one is wrong:
 *
 *   - first argument of `KAUTH_HELPER_MAIN()` (helper side: which bus name it owns)
 *   - `KAuth::Action::setHelperId()` (session side: without it the polkit backend refuses outright)
 *   - action name prefix in `<helper>.actions` (policy side)
 *   - `allow own` in `/usr/share/dbus-1/system.d/<helper>.conf` (bus side)
 *
 * `tst_kauth_wiring` pins all four together.
 */
inline constexpr auto kHelperId = "org.kde.kcm.docker";

/*!
 * Action id (must match the section names in `kauth/org.kde.kcm.docker.actions`).
 *
 * Only **lowercase letters and digits** are allowed, with `.` for hierarchy: the official tutorial
 * requires it, and KAuth's own kauth-policy-gen rejects uppercase and underscores outright
 * (`Wrong action syntax`), so names like `write_daemon_config` are impossible here.
 *
 * Action name → helper slot: drop the helper id prefix and replace `.` with `_`
 * (`org.kde.kcm.docker.daemon.save` → `daemon_save`).
 */
inline constexpr auto kSaveActionName = "org.kde.kcm.docker.daemon.save";
inline constexpr auto kRestartActionName = "org.kde.kcm.docker.daemon.restart";

/*!
 * Restricted semantics of a privilege request (ARCH_V5_V8 §2.4).
 *
 * **This is the security boundary of the privileged component**: the helper accepts only "edit
 * intents for whitelisted keys" — never arbitrary JSON, paths or commands. It reads the target
 * file, merges and writes it itself, so no caller can make it write anything off the whitelist.
 *
 * The class deliberately avoids KAuth / QtWidgets / DBus: the helper uses it, and unit tests use it
 * directly (no root needed to verify that out-of-scope requests are rejected).
 */
class PrivilegedConfigRequest
{
public:
    /*! Whitelisted keys (the same set DaemonConfigDocument manages). */
    static QStringList allowedKeys();
    /*! Additional allowed argument keys (switches of the request itself, not daemon.json keys). */
    static QStringList allowedControlKeys();

    /*!
     * Keys that may be **deleted** (returning to the daemon default).
     *
     * "Set to the default value" is not the same as "delete the key": the daemon's own default
     * changes between versions, and the user may have written that key by hand. Deletion therefore
     * travels as its own intent (control key `remove`, value = list of key names) and only accepts
     * keys we manage.
     */
    static QStringList removableKeys();

    /*! Log driver whitelist: only these values are accepted (anything else is rejected). */
    static QStringList allowedLogDrivers();

    /*!
     * Parse the edit intent from KAuth arguments.
     *
     * On failure returns false plus a reason key (`unknownKey` / `invalidValue` / `tooLarge` /
     * `noEdits` / `conflictingKeys`: a key both assigned and marked for removal).
     * **Any unrecognized key rejects the whole request** instead of being ignored — ignoring unknown
     * arguments would let the caller believe the request took effect.
     */
    static bool fromArguments(const QVariantMap &arguments, PrivilegedConfigRequest *request, QString *errorKey);

    bool isEmpty() const
    {
        return !m_setRegistryMirrors && !m_setInsecureRegistries && !m_setMaxConcurrentDownloads && !m_setLogDriver
            && m_removeKeys.isEmpty();
    }
    /*!
     * Validate only, never write.
     *
     * Used by the UI's "Unlock" button to trigger authorization: polkit remembers "keep" per
     * **action**, so the same action id must be used to really warm up later saves. The helper then
     * returns success once the dryRun request validates, without touching the disk.
     */
    bool dryRun() const
    {
        return m_dryRun;
    }
    /*! Keys requested for removal (validated names of keys we manage). */
    QStringList removeKeys() const
    {
        return m_removeKeys;
    }
    bool setRegistryMirrors() const
    {
        return m_setRegistryMirrors;
    }
    QStringList registryMirrors() const
    {
        return m_registryMirrors;
    }
    bool setInsecureRegistries() const
    {
        return m_setInsecureRegistries;
    }
    QStringList insecureRegistries() const
    {
        return m_insecureRegistries;
    }
    bool setMaxConcurrentDownloads() const
    {
        return m_setMaxConcurrentDownloads;
    }
    int maxConcurrentDownloads() const
    {
        return m_maxConcurrentDownloads;
    }
    bool setLogDriver() const
    {
        return m_setLogDriver;
    }
    QString logDriver() const
    {
        return m_logDriver;
    }

    /*!
     * Merge the edit intent into the existing file content.
     *
     * Unparsable existing content yields an empty array (the helper then refuses to write — a file
     * it cannot read is never overwritten). Unknown keys are preserved, per
     * `DaemonConfigDocument` semantics.
     */
    QByteArray mergeInto(const QByteArray &existingContent) const;

    /*! Byte cap for request content (keeps the config from becoming hundreds of KB of junk). */
    static constexpr int kMaxContentBytes = 64 * 1024;
    /*! Maximum number of entries per list. */
    static constexpr int kMaxListEntries = 32;

private:
    bool m_setRegistryMirrors = false;
    QStringList m_registryMirrors;
    bool m_setInsecureRegistries = false;
    QStringList m_insecureRegistries;
    bool m_setMaxConcurrentDownloads = false;
    int m_maxConcurrentDownloads = 0;
    bool m_setLogDriver = false;
    QString m_logDriver;
    QStringList m_removeKeys;
    bool m_dryRun = false;
};

} // namespace Kontainer
