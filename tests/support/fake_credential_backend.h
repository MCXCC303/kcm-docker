/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/credential_store.h"

#include <QHash>

namespace Kontainer
{

/*!
 * In-memory credential backend (test double).
 *
 * KWallet is unavailable in CI and headless environments, yet `CredentialStore`'s logic
 * (index-key normalization, entry format, overwrite/remove, availability state machine) is
 * wallet-independent and must be verified deterministically anywhere.
 *
 * Switches simulate the three real cases: available, wallet disabled, user refusing to unlock.
 */
class FakeCredentialBackend : public CredentialBackend
{
public:
    /*! Whether open succeeds (false = user refused to unlock / cannot be opened). */
    bool openSucceeds = true;
    /*! Whether the wallet subsystem is enabled (false = user turned the wallet off). */
    bool enabled = true;
    /*! Whether writes fail (simulates a read-only wallet or a disk error). */
    bool writeFails = false;
    /*! Whether open calls back immediately (false = simulates KWallet's async open). */
    bool synchronousOpen = true;

    bool isAvailable() const override
    {
        return m_open;
    }

    void open(const std::function<void(bool)> &callback) override
    {
        if (!enabled) {
            m_reason = QStringLiteral("walletDisabled");
            callback(false);
            return;
        }
        ++openRequests;
        const auto finish = [this, callback](bool success) {
            m_open = success;
            if (!success) {
                m_reason = QStringLiteral("walletOpenFailed");
            }
            callback(success);
        };
        if (synchronousOpen) {
            finish(openSucceeds);
            return;
        }
        m_pendingOpen = finish; // the test calls completeOpen()
    }

    /*! Finish an async open (used when `synchronousOpen == false`). */
    void completeOpen()
    {
        auto pending = m_pendingOpen;
        m_pendingOpen = nullptr;
        if (pending) {
            pending(openSucceeds);
        }
    }

    QStringList keys() const override
    {
        return entries.keys();
    }

    QByteArray read(const QString &key) const override
    {
        return entries.value(key);
    }

    bool write(const QString &key, const QByteArray &value) override
    {
        if (writeFails) {
            return false;
        }
        entries.insert(key, value);
        return true;
    }

    bool remove(const QString &key) override
    {
        entries.remove(key);
        return true;
    }

    QString unavailableReason() const override
    {
        return m_reason;
    }

    int openRequests = 0;
    /*! Inject a raw entry directly (simulates content left by an older version or manual editing). */
    QHash<QString, QByteArray> entries;

private:
    bool m_open = false;
    QString m_reason;
    std::function<void(bool)> m_pendingOpen;
};

} // namespace Kontainer
