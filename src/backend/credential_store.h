/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/registry_auth.h"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

namespace Kontainer
{

/*!
 * Key/value backend for credentials (ARCH_V5_V8 §2.6).
 *
 * KWallet is the only real implementation (`KWalletBackend`), but the interface exists separately
 * for two reasons:
 *   1. KWallet is unavailable in CI/headless, yet key normalization, entry format, overwrite and
 *      delete must be unit-tested — none of that depends on the wallet;
 *   2. An unavailable wallet (not enabled / user refuses to unlock) is a **normal path**, not a
 *      crash: the store must express it so the UI can explain why.
 */
class CredentialBackend
{
public:
    virtual ~CredentialBackend() = default;

    /*! Can the backend read/write right now (for KWallet: the wallet is open)? */
    virtual bool isAvailable() const = 0;
    /*!
     * Request opening the backend (KWallet may prompt for unlock), then call `callback(success)`.
     *
     * Synchronous implementations may call back inline; asynchronous ones must call back exactly once.
     */
    virtual void open(const std::function<void(bool)> &callback) = 0;

    /*! Keys of existing entries (order not guaranteed). */
    virtual QStringList keys() const = 0;
    /*! Read a key; empty on missing key or failure. */
    virtual QByteArray read(const QString &key) const = 0;
    /*! Write/overwrite; false on failure. */
    virtual bool write(const QString &key, const QByteArray &value) = 0;
    /*! Remove; a missing entry also counts as success. */
    virtual bool remove(const QString &key) = 0;

    /*! Reason key when unavailable (`walletDisabled` / `walletOpenFailed`…), may be empty. */
    virtual QString unavailableReason() const
    {
        return {};
    }
};

/*!
 * Registry credential store (ARCH_V5_V8 §2.6).
 *
 * Responsibility split:
 *   - **here**: key normalization (`RegistryAuth::normalizeServerAddress`), entry encoding,
 *     sorting/dedup, availability state machine;
 *   - **backend**: actual persistence (KWallet).
 *
 * Credentials live only in memory and the wallet: never written back to `~/.docker/config.json`,
 * never logged, never in error text (§40). `serverAddresses()` returns addresses only, no values.
 */
class CredentialStore : public QObject
{
    Q_OBJECT

public:
    enum class State {
        /*! Opening has not been requested yet. */
        Closed,
        /*! Opening (KWallet may be waiting for the user to unlock). */
        Opening,
        /*! Usable. */
        Ready,
        /*! Unavailable: wallet not enabled / refused / cannot open. */
        Unavailable,
    };
    Q_ENUM(State)

    explicit CredentialStore(CredentialBackend *backend, QObject *parent = nullptr);
    ~CredentialStore() override;

    /*! Open the backend (idempotent; returns the current state if already usable or failed). */
    void open();

    State state() const;
    /*! Availability reason key (meaningful only in `Unavailable`). */
    QString unavailableReason() const;

    /*! Registries with stored credentials (normalized, alphabetical); empty when not ready. */
    QStringList serverAddresses() const;
    bool hasCredential(const QString &serverAddress) const;
    /*! Read; returns empty for a missing registry or corrupt data (never hands out half a credential). */
    RegistryCredential credential(const QString &serverAddress) const;
    /*!
     * Credential for an image reference (`ghcr.io/team/app:1` → `ghcr.io`).
     *
     * This is all pull/build need: server-address parsing lives only in `RegistryAuth`,
     * so callers must not split references themselves.
     */
    RegistryCredential credentialForImage(const QString &imageReference) const;

    /*!
     * Store (overwrites an entry with the same name).
     *
     * Failure keys: `invalidServerAddress` (empty address), `noCredentials` (no username or token),
     * `unavailable` (backend not usable), `writeFailed`.
     */
    bool store(const RegistryCredential &credential, QString *errorKey = nullptr);
    /*! Remove; `invalidServerAddress` / `unavailable` return false. */
    bool remove(const QString &serverAddress, QString *errorKey = nullptr);

    /* ---------------- Entry format (public for unit tests and future migrations) ---------------- */

    /*!
     * Entry value: compact JSON, **without** `serveraddress` (that is the key).
     *
     * JSON instead of a `username:password` concatenation: passwords may contain colons, quotes
     * or even newlines, which such a format would eventually mis-parse; it also fits
     * `identitytoken` logins.
     */
    static QByteArray encodeEntry(const RegistryCredential &credential);
    /*! Decode an entry value; corruption gives empty plus errorKey (`invalidJson` / `noCredentials`). */
    static RegistryCredential decodeEntry(const QString &serverAddress, const QByteArray &value, QString *errorKey = nullptr);

Q_SIGNALS:
    /*! Content changed (added / overwritten / removed). */
    void changed();
    /*! `state()` changed. */
    void stateChanged();

private:
    void setState(State state);

    CredentialBackend *m_backend = nullptr;
    State m_state = State::Closed;
    QString m_unavailableReason;
};

} // namespace Kontainer
