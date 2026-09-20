/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace Kontainer
{

/*!
 * Edit intent for `daemon.json` (ARCH_V5_V8 §2.3).
 *
 * Covers only "the keys we manage"; all other keys are **preserved verbatim** on merge —
 * users may have configured data-root, features, runtimes or other things we do not understand,
 * and losing them is the least acceptable way for a "config editor" to fail.
 */
/*!
 * Edit intent for scalar keys (ARCH_V5_V8 §2.3).
 *
 * Why three states instead of overloading the value range: concurrent downloads once used `<= 0`
 * for "do not modify", so there was **no way to express "delete this key, back to the daemon
 * default"** — after setting a wrong log driver the user could only revert from a backup or edit
 * by hand. Keeping Set and Remove apart lets the UI offer a "Default" choice.
 */
enum class ConfigEdit {
    /*! Leave this key alone (keep the file's value verbatim, even if we do not understand it). */
    Unchanged,
    /*! Write a new value. */
    Set,
    /*! Delete this key (back to the daemon's own default). */
    Remove,
};

struct DaemonConfigEdits {
    /*! Whether to modify the registry-mirror list. */
    bool setRegistryMirrors = false;
    QStringList registryMirrors;
    /*! Whether to modify the insecure-registry list. */
    bool setInsecureRegistries = false;
    QStringList insecureRegistries;
    /*! Concurrent downloads: Set takes maxConcurrentDownloads, Remove deletes the key. */
    ConfigEdit concurrentDownloadsEdit = ConfigEdit::Unchanged;
    int maxConcurrentDownloads = 0;
    /*! Log driver: Set takes logDriver, Remove deletes the key. */
    ConfigEdit logDriverEdit = ConfigEdit::Unchanged;
    QString logDriver;

    /*! Whether nothing was changed at all. */
    bool isEmpty() const
    {
        return !setRegistryMirrors && !setInsecureRegistries && concurrentDownloadsEdit == ConfigEdit::Unchanged
            && logDriverEdit == ConfigEdit::Unchanged;
    }
};

/*!
 * Reading and writing `daemon.json` (ARCH_V5_V8 §2.3).
 *
 * Both directions work on a JSON document that tolerates unknown fields:
 *  - read: parse OK → whitelisted keys can be read; parse failure → show the error read-only,
 *    **never overwrite**
 *  - write: merge whitelisted keys into the original document, keeping unknown keys one by one
 */
class DaemonConfigDocument
{
public:
    /*! Read from file; a missing file yields an empty document (`exists=false`). */
    static DaemonConfigDocument fromFile(const QString &path);

    /*! Build from in-memory content (the privileged helper reuses the same parse/merge semantics). */
    static DaemonConfigDocument fromContent(const QByteArray &content, const QString &path = QString());

    bool exists() const
    {
        return m_exists;
    }
    bool isValid() const
    {
        return m_valid;
    }
    QString errorText() const
    {
        return m_errorText;
    }
    QString path() const
    {
        return m_path;
    }
    QJsonObject root() const
    {
        return m_root;
    }

    /*! Raw file content (for byte-level preservation checks and backup validation). */
    QByteArray rawContent() const
    {
        return m_rawContent;
    }

    /* --- Whitelisted key readers --- */
    QStringList registryMirrors() const;
    QStringList insecureRegistries() const;
    int maxConcurrentDownloads() const;
    QString logDriver() const;
    QString dataRoot() const;
    QString storageDriver() const;
    /*! Names of keys other than the ones we manage (UI shows "Other keys: N (read-only)"). */
    QStringList unmanagedKeys() const;

    /*! The keys we manage (this order is the UI order). */
    static QStringList managedKeys();

    /*!
     * Merge the edit intent and serialize.
     *
     * An empty array means the merge failed (e.g. the original document did not parse). Format:
     * 2-space indent plus a trailing newline, matching Docker's docs; key order comes from
     * QJsonObject (JSON objects are unordered, so semantics are unaffected).
     */
    QByteArray merged(const DaemonConfigEdits &edits) const;

private:
    QString m_path;
    bool m_exists = false;
    bool m_valid = false;
    QString m_errorText;
    QJsonObject m_root;
    QByteArray m_rawContent;
};

/*!
 * Write-back and backup (ARCH_V5_V8 §2.4 "atomic write + backup").
 *
 * Only user-writable paths are written here; system paths go through the helper, which reuses
 * the same merge logic.
 */
class DaemonConfigWriter
{
public:
    /*! Backup file prefix: `daemon.json.kontainer-backup-<UTC timestamp>`. */
    static QString backupPrefix();

    /*!
     * Atomic write: write a temp file in the same directory, then `rename` over the target.
     * The existing file is backed up first (if present). Empty on success, otherwise the reason
     * (technical detail, not UI text).
     */
    static QString writeAtomically(const QString &path, const QByteArray &content, QString *backupPath = nullptr);

    /*! List existing backups for a config file (newest first). */
    static QStringList listBackups(const QString &path);

    /*! Read a backup's content (for "restore previous version"). */
    static QByteArray readBackup(const QString &backupPath);
};

} // namespace Kontainer
