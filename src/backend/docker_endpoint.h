/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QMetaType>
#include <QString>

namespace Kontainer
{

/*!
 * Access endpoint for the Docker Engine (ARCH_V1 §9).
 *
 * Phase 1 implements LocalUnixSocketEndpoint only. The socket path is not hardcoded:
 * DOCKER_HOST wins, otherwise rootless and system defaults are probed in order.
 * TcpEndpoint / SshEndpoint are future work; phase 1 has no remote endpoints.
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
    /*! Technical display name, e.g. unix:///var/run/docker.sock; empty without a valid endpoint. */
    QString displayName() const;
    /*!
     * Technical reason the endpoint is unavailable/unsupported (not translatable, and
     * free of the raw DOCKER_HOST value so possible credentials stay out of logs, §25).
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
