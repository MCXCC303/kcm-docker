/*
    SPDX-FileCopyrightText: 2026 kontainer developers
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
 * KWallet 后端（ARCH_V5_V8 §2.6）：凭据的唯一持久化位置。
 *
 * 为什么是"钱包 + 专用文件夹"而不是 `~/.docker/config.json`：
 * 后者是明文 base64（等价于明文密码），而且 Docker CLI 会重写它；
 * 钱包条目则受会话解锁保护，且不会因为用户跑一次 `docker login` 就被覆盖。
 *
 * 条目布局：钱包 `kdewallet` 的 `Kontainer` 文件夹，键 = 规范化后的仓库地址，
 * 值 = `CredentialStore::encodeEntry()` 的 JSON。**读的时候也只看这个文件夹**，
 * 不去翻别的应用的条目。
 *
 * 生命周期：`open()` 是异步的（钱包可能要求用户解锁）。未打开时 `isAvailable()`
 * 为 false，`CredentialStore` 会停在 `Unavailable` 并让界面说明原因——
 * 钱包不可用是正常路径，不是崩溃路径。
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

    /*! 钱包名（默认用网络钱包，与 KDE 其他应用一致）。 */
    static QString walletName();
    /*! 专用文件夹名。 */
    static QString folderName();

private:
    bool useFolder() const;

    std::unique_ptr<KWallet::Wallet> m_wallet;
    /*! 打开失败/不可用的原因 key（界面据此给文案）。 */
    QString m_unavailableReason;
    /*! 正在等待 walletOpened() 的回调（钱包是异步的）。 */
    std::function<void(bool)> m_pendingOpen;
};

} // namespace Kontainer
