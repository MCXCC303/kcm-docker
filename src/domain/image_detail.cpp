/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "domain/image_detail.h"

#include <QStringList>

namespace Kontainer
{

namespace
{
constexpr auto digestPrefix = "sha256:";

QStringList partsOf(const QString &tag)
{
    return tag.split(QLatin1Char(':'));
}
} // namespace

QString ImageDetail::primaryRepository() const
{
    for (const QString &tag : repoTags) {
        if (tag == QLatin1String("<none>:<none>")) {
            continue;
        }
        const QStringList parts = partsOf(tag);
        if (parts.size() >= 2) {
            // A tag can be host:port/path:tag, so only the last ':' starts the tag
            const int lastColon = tag.lastIndexOf(QLatin1Char(':'));
            const int lastSlash = tag.lastIndexOf(QLatin1Char('/'));
            if (lastColon > lastSlash) {
                return tag.left(lastColon);
            }
        }
        return tag;
    }
    return {};
}

QString ImageDetail::primaryTag() const
{
    for (const QString &tag : repoTags) {
        if (tag == QLatin1String("<none>:<none>")) {
            continue;
        }
        const int lastColon = tag.lastIndexOf(QLatin1Char(':'));
        const int lastSlash = tag.lastIndexOf(QLatin1Char('/'));
        if (lastColon > lastSlash) {
            return tag.mid(lastColon + 1);
        }
        return QStringLiteral("latest");
    }
    return {};
}

QString ImageDetail::shortId() const
{
    QString bare = id;
    if (bare.startsWith(QLatin1String(digestPrefix))) {
        bare.remove(0, int(sizeof(digestPrefix)) - 1);
    }
    return bare.left(12);
}

} // namespace Kontainer
