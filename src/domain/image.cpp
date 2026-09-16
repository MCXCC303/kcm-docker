/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "domain/image.h"

#include <QTimeZone>

namespace Kontainer
{

namespace
{
constexpr int shortIdLength = 12;
constexpr auto digestPrefix = "sha256:";
}

QString Image::shortId() const
{
    QString bare = id;
    if (bare.startsWith(QLatin1String(digestPrefix))) {
        bare.remove(0, int(sizeof(digestPrefix)) - 1);
    }
    return bare.left(shortIdLength);
}

QString Image::primaryTag() const
{
    for (const QString &tag : repoTags) {
        if (tag != QLatin1String("<none>:<none>")) {
            return tag;
        }
    }
    return {};
}

bool Image::isDangling() const
{
    return primaryTag().isEmpty();
}

} // namespace Kontainer
