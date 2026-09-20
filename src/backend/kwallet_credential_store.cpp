/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/kwallet_credential_store.h"

#include "logging.h"

#include <KWallet>

namespace Kontainer
{

namespace
{
/*! Wallet entries hold only these two fields; anything else read back counts as corrupt (no guessing). */
/*
 * The KWallet folder name keeps its old value on purpose: the app was renamed from
 * kontainer to kcm-docker, but stored credentials live in this folder -- renaming it
 * would make them vanish (a re-login per registry), so it stays.
 */
constexpr auto kFolder = "Kontainer";
} // namespace

KWalletBackend::KWalletBackend(QObject *parent)
    : QObject(parent)
{
}

KWalletBackend::~KWalletBackend() = default;

QString KWalletBackend::walletName()
{
    return KWallet::Wallet::NetworkWallet();
}

QString KWalletBackend::folderName()
{
    return QString::fromLatin1(kFolder);
}

bool KWalletBackend::isAvailable() const
{
    return m_wallet && m_wallet->isOpen();
}

QString KWalletBackend::unavailableReason() const
{
    return m_unavailableReason;
}

void KWalletBackend::open(const std::function<void(bool)> &callback)
{
    if (isAvailable()) {
        callback(true);
        return;
    }

    // Do not open the wallet when the user disabled it (it fails and nags the user)
    if (!KWallet::Wallet::isEnabled()) {
        m_unavailableReason = QStringLiteral("walletDisabled");
        callback(false);
        return;
    }

    // An open is already pending: attach this callback, do not fire a second request
    const auto finish = [this, callback](bool success) {
        if (!success) {
            m_unavailableReason = m_unavailableReason.isEmpty() ? QStringLiteral("walletOpenFailed") : m_unavailableReason;
        }
        callback(success);
    };

    m_pendingOpen = finish;
    KWallet::Wallet *wallet = KWallet::Wallet::openWallet(walletName(), 0, KWallet::Wallet::Asynchronous);
    if (!wallet) {
        m_pendingOpen = nullptr;
        m_unavailableReason = QStringLiteral("walletOpenFailed");
        callback(false);
        return;
    }
    m_wallet.reset(wallet);
    connect(m_wallet.get(), &KWallet::Wallet::walletOpened, this, [this](bool success) {
        if (!success) {
            m_wallet.reset();
            m_unavailableReason = QStringLiteral("walletOpenFailed");
        } else if (!useFolder()) {
            // Opened but no folder: a failed createFolder is unusable too (I/O would hit the wrong folder)
            m_unavailableReason = QStringLiteral("walletFolderFailed");
            m_wallet.reset();
            success = false;
        } else {
            qCDebug(kontainerModel) << "wallet opened:" << walletName() << "folder:" << folderName();
        }
        auto pending = m_pendingOpen;
        m_pendingOpen = nullptr;
        if (pending) {
            pending(success);
        }
    });
}

bool KWalletBackend::useFolder() const
{
    if (!m_wallet) {
        return false;
    }
    // Read/write only inside our own folder, never other apps' entries
    if (m_wallet->setFolder(folderName())) {
        return true;
    }
    return m_wallet->createFolder(folderName()) && m_wallet->setFolder(folderName());
}

QStringList KWalletBackend::keys() const
{
    if (!isAvailable() || !useFolder()) {
        return {};
    }
    return m_wallet->entryList();
}

QByteArray KWalletBackend::read(const QString &key) const
{
    if (!isAvailable() || !useFolder()) {
        return {};
    }
    QByteArray value;
    if (m_wallet->readEntry(key, value) != 0) {
        return {};
    }
    return value;
}

bool KWalletBackend::write(const QString &key, const QByteArray &value)
{
    if (!isAvailable() || !useFolder()) {
        return false;
    }
    return m_wallet->writeEntry(key, value) == 0;
}

bool KWalletBackend::remove(const QString &key)
{
    if (!isAvailable() || !useFolder()) {
        return false;
    }
    if (!m_wallet->hasEntry(key)) {
        return true; // a missing entry counts as deleted
    }
    return m_wallet->removeEntry(key) == 0;
}

} // namespace Kontainer
