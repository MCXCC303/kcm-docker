/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/credential_store.h"

#include <QHash>

namespace Kontainer
{

/*!
 * 内存版凭据后端（测试替身）。
 *
 * KWallet 在 CI 与无桌面环境里不可用，而 `CredentialStore` 的逻辑
 * （索引键规范化、条目格式、覆盖/删除、可用性状态机）与钱包无关，
 * 必须能在任何环境下确定性验证。
 *
 * 通过开关模拟三种现实情形：可用、钱包被禁用、用户拒绝解锁。
 */
class FakeCredentialBackend : public CredentialBackend
{
public:
    /*! 打开时是否成功（false = 用户拒绝解锁 / 打不开）。 */
    bool openSucceeds = true;
    /*! 包子系统是否启用（false = 用户关掉了钱包）。 */
    bool enabled = true;
    /*! 写入是否失败（模拟钱包只读/磁盘错误）。 */
    bool writeFails = false;
    /*! 打开时是否立即回调（false = 模拟 KWallet 的异步打开）。 */
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
        m_pendingOpen = finish; // 由测试调用 completeOpen()
    }

    /*! 完成一次异步打开（`synchronousOpen == false` 时使用）。 */
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
    /*! 直接塞一条原始条目（模拟旧版本或手工编辑留下的内容）。 */
    QHash<QString, QByteArray> entries;

private:
    bool m_open = false;
    QString m_reason;
    std::function<void(bool)> m_pendingOpen;
};

} // namespace Kontainer
