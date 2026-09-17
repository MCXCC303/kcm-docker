/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "domain/volume.h"

namespace Kontainer
{

bool Volume::operator==(const Volume &other) const
{
    return name == other.name && driver == other.driver && mountpoint == other.mountpoint && createdAt == other.createdAt
        && scope == other.scope && labels == other.labels && options == other.options && sizeBytes == other.sizeBytes
        && refCount == other.refCount && status == other.status;
}

} // namespace Kontainer
