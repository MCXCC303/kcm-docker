/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QString>

#include <optional>

namespace Kontainer
{

/*!
 * Docker Engine API version policy (ARCH_V1 §8).
 *
 * The only place in the project where "v1.xx" may appear: no other source file holds version
 * strings, they get the path prefix through this type.
 *
 * Negotiation: GET /version → server ApiVersion and MinAPIVersion →
 * min(server ApiVersion, client maximum) within the client's supported range.
 */
class ApiVersion
{
public:
    constexpr ApiVersion() = default;
    constexpr ApiVersion(int major, int minor)
        : m_major(major)
        , m_minor(minor)
    {
    }

    /*! Lowest Engine API version the client supports (since Docker 20.10). */
    static constexpr int clientMinMinor()
    {
        return 41;
    }
    /*! Highest Engine API version the client supports (Docker 29.x). */
    static constexpr int clientMaxMinor()
    {
        return 56;
    }

    /*! Parses version strings like "1.56"; invalid input returns nullopt. */
    static std::optional<ApiVersion> fromString(const QString &version);

    /*!
     * Negotiates a version both sides support, within the client's range.
     * Returns nullopt when the server is too old (below the client minimum) or too new (its
     * required minimum is above the client maximum); callers map that to
     * DockerError::Kind::ApiVersionMismatch.
     */
    static std::optional<ApiVersion> negotiate(const ApiVersion &server, const std::optional<ApiVersion> &serverMin);

    bool isValid() const
    {
        return m_major > 0;
    }
    int major() const
    {
        return m_major;
    }
    int minor() const
    {
        return m_minor;
    }

    QString toString() const;
    /*! Request path prefix, e.g. "v1.56". */
    QString pathPrefix() const;

    friend bool operator==(const ApiVersion &lhs, const ApiVersion &rhs)
    {
        return lhs.m_major == rhs.m_major && lhs.m_minor == rhs.m_minor;
    }
    friend bool operator<(const ApiVersion &lhs, const ApiVersion &rhs)
    {
        return lhs.m_major != rhs.m_major ? lhs.m_major < rhs.m_major : lhs.m_minor < rhs.m_minor;
    }
    friend bool operator>(const ApiVersion &lhs, const ApiVersion &rhs)
    {
        return rhs < lhs;
    }

private:
    int m_major = 0;
    int m_minor = 0;
};

} // namespace Kontainer
