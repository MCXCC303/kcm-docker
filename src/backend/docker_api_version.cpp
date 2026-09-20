/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/docker_api_version.h"

#include <QStringList>

#include <algorithm>

namespace Kontainer
{

std::optional<ApiVersion> ApiVersion::fromString(const QString &version)
{
    const QString trimmed = version.trimmed();
    if (trimmed.isEmpty()) {
        return std::nullopt;
    }

    const QStringList parts = trimmed.split(QLatin1Char('.'));
    if (parts.size() != 2) {
        return std::nullopt;
    }

    bool majorOk = false;
    bool minorOk = false;
    const int major = parts.at(0).toInt(&majorOk);
    const int minor = parts.at(1).toInt(&minorOk);
    if (!majorOk || !minorOk || major <= 0 || minor < 0) {
        return std::nullopt;
    }
    return ApiVersion(major, minor);
}

std::optional<ApiVersion> ApiVersion::negotiate(const ApiVersion &server, const std::optional<ApiVersion> &serverMin)
{
    if (!server.isValid() || server.major() != 1) {
        // Engine API v1.x only
        return std::nullopt;
    }
    if (server.minor() < clientMinMinor()) {
        // Server too old, below the client minimum
        return std::nullopt;
    }
    if (serverMin.has_value() && serverMin->major() == 1 && serverMin->minor() > clientMaxMinor()) {
        // The server's required minimum is above the client maximum
        return std::nullopt;
    }
    return ApiVersion(1, std::min(server.minor(), clientMaxMinor()));
}

QString ApiVersion::toString() const
{
    return QStringLiteral("%1.%2").arg(m_major).arg(m_minor);
}

QString ApiVersion::pathPrefix() const
{
    return QStringLiteral("v") + toString();
}

} // namespace Kontainer
