/*
    SPDX-FileCopyrightText: 2026 kontainer developers
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
 * 凭据的键值后端（ARCH_V5_V8 §2.6）。
 *
 * KWallet 是唯一的真实实现（`KWalletBackend`），但接口单独存在有两个理由：
 *   1. KWallet 在 CI/无桌面环境里不可用，而"索引键规范化、条目格式、覆盖与删除"
 *      这些逻辑必须能被单测覆盖——它们与钱包无关；
 *   2. 钱包不可用（未启用 / 用户拒绝解锁）是**正常路径**而不是崩溃路径，
 *      存储层要能把这件事表达清楚，让界面去说明原因。
 */
class CredentialBackend
{
public:
    virtual ~CredentialBackend() = default;

    /*! 后端现在就能读写吗（KWallet：钱包已经打开）。 */
    virtual bool isAvailable() const = 0;
    /*!
     * 请求打开后端（KWallet 可能弹解锁框），完成后调用 `callback(success)`。
     *
     * 同步实现可以直接回调；异步实现必须保证回调恰好一次。
     */
    virtual void open(const std::function<void(bool)> &callback) = 0;

    /*! 现有条目的键（顺序不保证）。 */
    virtual QStringList keys() const = 0;
    /*! 读取某个键；不存在或失败返回空。 */
    virtual QByteArray read(const QString &key) const = 0;
    /*! 写入/覆盖；失败返回 false。 */
    virtual bool write(const QString &key, const QByteArray &value) = 0;
    /*! 删除；不存在也算成功。 */
    virtual bool remove(const QString &key) = 0;

    /*! 不可用时的原因 key（`walletDisabled` / `walletOpenFailed`…），可为空。 */
    virtual QString unavailableReason() const
    {
        return {};
    }
};

/*!
 * 仓库凭据存储（ARCH_V5_V8 §2.6）。
 *
 * 职责边界：
 *   - **这里**：索引键规范化（`RegistryAuth::normalizeServerAddress`）、条目编码、
 *     排序去重、可用性状态机；
 *   - **后端**：真正的持久化（KWallet）。
 *
 * 凭据只存在于内存与钱包里：不写回 `~/.docker/config.json`、不进日志、
 * 不出现在错误文案里（§40）。`serverAddresses()` 只返回仓库地址，不返回值。
 */
class CredentialStore : public QObject
{
    Q_OBJECT

public:
    enum class State {
        /*! 还没请求打开。 */
        Closed,
        /*! 正在打开（KWallet 可能正在等用户解锁）。 */
        Opening,
        /*! 可用。 */
        Ready,
        /*! 不可用：钱包未启用 / 被拒绝 / 打不开。 */
        Unavailable,
    };
    Q_ENUM(State)

    explicit CredentialStore(CredentialBackend *backend, QObject *parent = nullptr);
    ~CredentialStore() override;

    /*! 打开后端（幂等；已可用或已失败时直接返回当前状态）。 */
    void open();

    State state() const;
    /*! 可用性原因 key（仅 `Unavailable` 时有意义）。 */
    QString unavailableReason() const;

    /*! 已存凭据的仓库（规范化地址，按字母序）；未就绪时为空。 */
    QStringList serverAddresses() const;
    bool hasCredential(const QString &serverAddress) const;
    /*! 读取；没有该仓库或内容损坏时返回空凭据（不会把半条凭据交给调用方）。 */
    RegistryCredential credential(const QString &serverAddress) const;
    /*!
     * 按镜像引用取凭据（`ghcr.io/team/app:1` → `ghcr.io`）。
     *
     * 拉取/构建只需要这一句：仓库地址的解析规则只有 `RegistryAuth` 一处实现，
     * 调用方不该自己拆引用。
     */
    RegistryCredential credentialForImage(const QString &imageReference) const;

    /*!
     * 保存（覆盖同名条目）。
     *
     * 失败原因 key：`invalidServerAddress`（地址为空）、`noCredentials`（没有用户名或令牌）、
     * `unavailable`（后端不可用）、`writeFailed`。
     */
    bool store(const RegistryCredential &credential, QString *errorKey = nullptr);
    /*! 删除；`invalidServerAddress` / `unavailable` 会返回 false。 */
    bool remove(const QString &serverAddress, QString *errorKey = nullptr);

    /* ---------------- 条目格式（公开以便单测与将来的迁移） ---------------- */

    /*!
     * 条目值：紧凑 JSON，**不含** `serveraddress`（它就是键）。
     *
     * 用 JSON 而不是 `用户名:密码` 这类拼接：密码里可以有冒号、引号甚至换行，
     * 拼接格式迟早会解析错；同时令牌登录（`identitytoken`）也能表达。
     */
    static QByteArray encodeEntry(const RegistryCredential &credential);
    /*! 解析条目值；损坏时返回空凭据并给出 errorKey（`invalidJson` / `noCredentials`）。 */
    static RegistryCredential decodeEntry(const QString &serverAddress, const QByteArray &value, QString *errorKey = nullptr);

Q_SIGNALS:
    /*! 内容变化（新增 / 覆盖 / 删除）。 */
    void changed();
    /*! `state()` 变化。 */
    void stateChanged();

private:
    void setState(State state);

    CredentialBackend *m_backend = nullptr;
    State m_state = State::Closed;
    QString m_unavailableReason;
};

} // namespace Kontainer
