/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/kwallet_credential_store.h"

#include "logging.h"

#include <KWallet>

namespace Kontainer
{

namespace
{
/*! 钱包条目只允许这两个字段；读到别的内容一律当损坏（不猜）。 */
/*
 * KWallet 的**目录名刻意保持旧值**：软件从 kontainer 更名为 kcm-docker，但用户已经存进去的
 * 仓库凭据就在这个目录下——改名字等于让它们凭空消失（还得重新登录每个仓库）。
 * 名字本身不影响功能，因此不动。
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

    // 钱包子系统被用户关掉时不要去开（会直接失败，还会打扰用户）
    if (!KWallet::Wallet::isEnabled()) {
        m_unavailableReason = QStringLiteral("walletDisabled");
        callback(false);
        return;
    }

    // 已经在等一次打开了：把回调挂上去，不再发起第二次请求
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
            // 打开成功但没有文件夹：新建失败也不能算可用（否则读写会落到错误的文件夹）
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
    // 只在我们自己的文件夹里读写：绝不碰别的应用的条目
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
        return true; // 不存在也算删除成功
    }
    return m_wallet->removeEntry(key) == 0;
}

} // namespace Kontainer
