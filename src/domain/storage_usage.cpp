/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "domain/storage_usage.h"

#include <algorithm>

namespace Kontainer
{

namespace
{
qint64 addIfKnown(qint64 total, qint64 value)
{
    return value > 0 ? total + value : total;
}
} // namespace

qint64 StorageUsage::totalBytes() const
{
    qint64 total = 0;
    total = addIfKnown(total, imagesBytes);
    total = addIfKnown(total, containersBytes);
    total = addIfKnown(total, volumesBytes);
    total = addIfKnown(total, buildCacheBytes);
    return total;
}

} // namespace Kontainer
