/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/credential_store.h"

#include <QObject>

#include <memory>

namespace KWallet
{
class Wallet;
}

namespace Kontainer
{

/*!
 * KWallet backend (ARCH_V5_V8 §2.6): the only persistent credential store.
 *
 * Why a wallet folder and not `~/.docker/config.json`: the latter is plain base64
 * (a plaintext password) and is rewritten by the Docker CLI; wallet entries are
 * protected by the session unlock and survive a `docker login`.
 *
 * Layout: folder `Kontainer` in wallet `kdewallet`; key = normalized registry host,
 * value = JSON from `CredentialStore::encodeEntry()`. Reads only ever look in that
 * folder, never at other applications' entries.
 *
 * `open()` is asynchronous (the wallet may prompt to unlock); while unopened,
 * `isAvailable()` is false, `CredentialStore` stays `Unavailable` and the UI explains
 * why -- an unavailable wallet is a normal path, not a crash path.
 */
class KWalletBackend : public QObject, public CredentialBackend
{
    Q_OBJECT

public:
    explicit KWalletBackend(QObject *parent = nullptr);
    ~KWalletBackend() override;

    bool isAvailable() const override;
    void open(const std::function<void(bool)> &callback) override;

    QStringList keys() const override;
    QByteArray read(const QString &key) const override;
    bool write(const QString &key, const QByteArray &value) override;
    bool remove(const QString &key) override;

    QString unavailableReason() const override;

    /*! Wallet name (the network wallet, like other KDE apps). */
    static QString walletName();
    /*! Dedicated folder name. */
    static QString folderName();

private:
    bool useFolder() const;

    std::unique_ptr<KWallet::Wallet> m_wallet;
    /*! Reason key for open failure/unavailability (the UI picks its text). */
    QString m_unavailableReason;
    /*! Callback waiting for walletOpened() (the wallet is asynchronous). */
    std::function<void(bool)> m_pendingOpen;
};

} // namespace Kontainer
