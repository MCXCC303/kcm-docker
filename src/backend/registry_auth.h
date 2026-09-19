/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QByteArray>
#include <QString>

namespace Kontainer
{

/*!
 * 一条仓库凭据（ARCH_V5_V8 §2.6）。
 *
 * 只存在于内存与 KWallet 里：不写回 `~/.docker/config.json`、不进日志、
 * 不出现在错误文案里（§40 的脱敏规则同样适用于它）。
 */
struct RegistryCredential {
    /*! 规范化后的仓库地址（见 `RegistryAuth::normalizeServerAddress()`），也是凭据的索引键。 */
    QString serverAddress;
    /*! 用户名 + 密码（或下面二选一的 identity token）。 */
    QString username;
    QString password;
    /*! 令牌登录（例如 CI 里发的 identity token）：给了它就不再发送用户名密码。 */
    QString identityToken;

    /*!
     * 是不是一条**完整**的凭据。
     *
     * 只有用户名没有密码不算：那既不能登录（引擎会 401），也不该被存进钱包
     * （用户下次会看到一条"看起来有、其实用不了"的条目）。
     */
    bool isEmpty() const
    {
        if (serverAddress.isEmpty()) {
            return true;
        }
        if (!identityToken.isEmpty()) {
            return false;
        }
        return username.isEmpty() || password.isEmpty();
    }
    /*! 是否用令牌登录。 */
    bool usesIdentityToken() const
    {
        return !identityToken.isEmpty();
    }
};

/*!
 * `X-Registry-Auth` 请求头的编解码（ARCH_V5_V8 §2.6）。
 *
 * Docker Engine 从该头读取拉取/构建/登录所需的凭据；格式是
 * `base64url(JSON)`，JSON 字段为 `username` / `password` / `serveraddress`，
 * 或 `identitytoken`。
 *
 * 为什么单独一个类：这个格式有三处容易踩的地方——URL-safe 字母表、padding、
 * 以及 Docker Hub 那个历史遗留的 `serveraddress` 写法（`https://index.docker.io/v1/`）。
 * 把规则放在一处并单测，好过在每个调用点各拼一遍。
 */
class RegistryAuth
{
public:
    /*! 规范化后的 Docker Hub 主机名（凭据索引键）。 */
    static constexpr auto hubHost = "index.docker.io";
    /*! 请求头里 Docker Hub 的 `serveraddress` 字段值（Docker 的历史约定，不能改）。 */
    static constexpr auto hubServerAddress = "https://index.docker.io/v1/";

    /*!
     * 规范化仓库地址，用作凭据的索引键。
     *
     * 规则：去首尾空白、去协议、去路径与末尾斜杠、主机名小写；
     * Docker Hub 的各种写法（`docker.io` / `index.docker.io` / `registry-1.docker.io`，
     * 带不带协议都算）统一成 `index.docker.io`。
     */
    static QString normalizeServerAddress(const QString &value);

    /*! 请求头 JSON 里 `serveraddress` 字段该写的值（Hub 用历史写法，其余用规范化后的地址）。 */
    static QString headerServerAddress(const QString &normalizedServerAddress);

    /*! 从镜像引用取仓库索引键（`alpine:3.19` → `index.docker.io`）。 */
    static QString serverAddressForImage(const QString &imageReference);

    /*!
     * 编码成 `X-Registry-Auth` 头的值：base64url(JSON)，与 docker CLI 一致
     * （URL-safe 字母表 + 保留 `=`，Docker 两种都收，但我们跟官方客户端保持一致）。
     */
    static QByteArray encode(const RegistryCredential &credential);

    /*!
     * 解析头的值：兼容 URL-safe 与标准 base64、带或不带 padding
     * （`~/.docker/config.json` 的 `auths` 用的是标准 base64）。
     *
     * 失败返回空凭据并给出 errorKey：`empty` / `invalidBase64` / `invalidJson` / `noCredentials`。
     */
    static RegistryCredential decode(const QByteArray &headerValue, QString *errorKey = nullptr);

    /*!
     * 解析 `~/.docker/config.json` 里 `auths` 的 `auth` 字段（标准 base64 的 `user:password`）。
     *
     * 与 `decode()` 分开：一个是 Docker API 的头，一个是 CLI 配置文件里的字段，
     * 两者格式不同（后者没有 JSON，只有 `用户名:密码`）。
     */
    static RegistryCredential decodeConfigAuth(const QString &serverAddress, const QByteArray &base64UserPassword, QString *errorKey = nullptr);

    /*!
     * `用户名:密码` → `config.json` 里 `auth` 字段的值（标准 base64，与 CLI 一致）。
     *
     * 与 `decodeConfigAuth()` 对称：写回 CLI 配置文件时用这个，保证 `docker login`
     * 之后 CLI 读到的就是我们写进去的同一份凭据。
     */
    static QString encodeConfigAuth(const QString &username, const QString &password);
};

} // namespace Kontainer
