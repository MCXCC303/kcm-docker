/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/credential_store.h"

#include <QString>

namespace Kontainer
{

/*!
 * 把凭据写回 Docker CLI 的 `config.json`（ARCH_V5_V8 §2.6 的后续调整）。
 *
 * 背景：凭据的**归属**仍然是 KWallet（不写明文、密码不出控制器），但用户希望
 * "在 Kontainer 里登录一次，`docker login` / `docker pull` 也能用"，而且**不要**
 * 界面上再出现"同步/导入"这类按钮——于是：
 *
 *   - 读取方向（CLI → 我们）由 `DockerCliAuthImporter` 在页面打开时**静默**完成；
 *   - 写入方向（我们 → CLI）由这里完成：新增/修改/删除凭据时同步一条 `auths` 条目。
 *
 * 三条安全约束：
 *   1. **绝不覆盖坏文件**：解析失败时返回错误、一个字都不写（用户的配置里可能有注释
 *      之外的东西，乱写会把他的登录状态弄丢）；
 *   2. **保留未知键**：`credsStore` / `credHelpers` / 别的仓库条目原样保留；
 *   3. **权限**：文件 0600、目录 0700（里面等价于明文凭据）。
 */
class DockerCliAuthWriter
{
public:
    /*! CLI 的配置文件路径：`$DOCKER_CONFIG/config.json`，否则 `~/.docker/config.json`。 */
    static QString defaultConfigPath();

    /*!
     * 新增或更新一条凭据（保留该条目里其它字段，例如 `identitytoken`）。
     *
     * @param errorKey 失败原因：`invalidJson` / `unwritable` / `noCredentials`。
     */
    static bool upsert(const QString &path,
                       const QString &serverAddress,
                       const RegistryCredential &credential,
                       QString *errorKey = nullptr);

    /*! 删除一条凭据（条目本来就不存在也算成功）。 */
    static bool remove(const QString &path, const QString &serverAddress, QString *errorKey = nullptr);

    /*!
     * 该仓库在 `auths` 里的键：优先沿用文件里已有的写法（同一个仓库可能被写成
     * `https://index.docker.io/v1/` 或 `index.docker.io`），否则用规范化地址。
     */
    static QString configKeyFor(const QString &path, const QString &serverAddress);
};

} // namespace Kontainer
