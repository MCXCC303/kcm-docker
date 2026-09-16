/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QMetaType>
#include <QString>

namespace Kontainer
{

/*!
 * Docker Engine 的访问端点（ARCH_V1 §9）。
 *
 * 一期只实现 LocalUnixSocketEndpoint。socket 路径不是硬编码业务逻辑：
 * 优先取 DOCKER_HOST，其次按 rootless/系统默认顺序探测。
 * 未来可扩展 TcpEndpoint / SshEndpoint，但一期不实现远程 endpoint。
 */
class DockerEndpoint
{
    Q_GADGET

public:
    enum class Type {
        Invalid,
        LocalUnixSocket,
    };
    Q_ENUM(Type)

    DockerEndpoint() = default;

    static DockerEndpoint unixSocket(const QString &path);
    static DockerEndpoint fromEnvironment();

    Type type() const
    {
        return m_type;
    }
    bool isValid() const
    {
        return m_type != Type::Invalid;
    }

    QString socketPath() const
    {
        return m_socketPath;
    }
    /*! 技术展示名，例如 unix:///var/run/docker.sock；无有效端点时为空。 */
    QString displayName() const;
    /*!
     * 端点不可用/不受支持的技术原因（不可翻译，且不含 DOCKER_HOST 原值，
     * 避免把可能的凭据带进日志，见 §25）。
     */
    QString problemDetail() const
    {
        return m_problem;
    }

private:
    Type m_type = Type::Invalid;
    QString m_socketPath;
    QString m_problem;
};

} // namespace Kontainer

Q_DECLARE_METATYPE(Kontainer::DockerEndpoint)
