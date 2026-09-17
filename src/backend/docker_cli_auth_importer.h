/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/credential_store.h"
#include "backend/registry_auth.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace Kontainer
{

/*! 从 `~/.docker/config.json` 读到的一条凭据。 */
struct ImportableCredential {
    RegistryCredential credential;
    /*! 配置文件里的原始键（例如 `https://index.docker.io/v1/`）：界面要如实说明来源。 */
    QString sourceKey;
};

/*! 一次导入扫描的结果。 */
struct DockerCliAuthScan {
    /*! 实际读取的路径。 */
    QString path;
    /*! 失败原因 key：空 = 成功；`missingFile` / `unreadable` / `invalidJson`。 */
    QString errorKey;
    /*! 可导入的凭据。 */
    QList<ImportableCredential> credentials;
    /*!
     * 由凭据助手（`credsStore` / `credHelpers`）管理的条目。
     *
     * 我们**不调用** credential helper（ARCH_V5_V8 §2.9）：它们会把凭据交给外部程序，
     * 既超出"只读导入"的范围，也会在用户不知情时触发系统钥匙串弹窗。
     * 但这些条目要让用户看见——否则他会奇怪"为什么我的仓库没被导入"。
     */
    QStringList helperManagedKeys;
    /*!
     * 读到了但用不上的条目：缺 `auth`、base64/字段坏掉，或与另一条指向同一个仓库。
     *
     * 界面对这些要如实汇报（"N 条无法导入"），而不是假装文件里只有能导入的那些。
     */
    QStringList skippedKeys;
    /*!
     * Docker 自己的令牌缓存条目（`<server>/access-token` / `<server>/refresh-token`）。
     *
     * 它们**不是**仓库凭据（值通常是 OAuth 令牌缓存），本机实测 `~/.docker/config.json`
     * 里就有两条：如果把它们的路径部分抹掉，就会和真正的 Hub 条目撞在同一个索引键上，
     * 甚至把缓存令牌当成密码导进钱包。因此显式识别并忽略。
     */
    QStringList tokenCacheKeys;
};

/*!
 * 从 Docker CLI 的 `config.json` 一次性**只读**导入凭据（ARCH_V5_V8 §2.6/§2.7）。
 *
 * 三件事被刻意限制住：
 *   1. **只读**：绝不写回该文件（它是 CLI 的，凭据的归属是 KWallet）；
 *   2. **不调用 credential helper**；
 *   3. **不覆盖**钱包里已有的条目（见 `importInto()`）——静默覆盖会让用户丢掉刚设好的密码。
 *
 * 注意：该文件里的 `auth` 是标准 base64 的 `用户名:密码`，等价于明文凭据；
 * 因此扫描结果只留在内存里，不写日志、不进错误文案。
 */
class DockerCliAuthImporter
{
public:
    /*! CLI 的配置文件路径：`$DOCKER_CONFIG/config.json`，否则 `~/.docker/config.json`。 */
    static QString defaultConfigPath();

    /*! 扫描配置（只读）。文件不存在不算错误，只是没有可导入的条目。 */
    static DockerCliAuthScan scan(const QString &path);

    /*! 一次导入的结果（用于界面如实汇报）。 */
    struct ImportOutcome {
        /*! 真正写进钱包的条数。 */
        int imported = 0;
        /*! 钱包里已有、因此**没有**覆盖的条数。 */
        int alreadyPresent = 0;
        /*! 写入失败的条数。 */
        int failed = 0;
    };

    /*!
     * 把扫描结果导入钱包：已有条目不覆盖。
     *
     * 这样"导入"是幂等的，也不会把用户刚在界面上改过的密码换回文件里的旧值。
     */
    static ImportOutcome importInto(CredentialStore &store, const DockerCliAuthScan &scan);
};

} // namespace Kontainer
