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
 * Engine-level Docker disk usage (`GET /system/df`) (ARCH_V2 §23/§24).
 *
 * The source is the structured API, not the `docker system df` CLI (§24). Categories may be absent
 * (API version, build cache present or not), so all are optional: `available == false` means the
 * category is unavailable and the UI shows "—" instead of 0.
 */
struct StorageUsage {
    bool valid = false; /*!< the response parsed successfully at all */
    bool buildCacheAvailable = false; /*!< whether the BuildCache field is present */

    qint64 imagesBytes = -1;
    qint64 containersBytes = -1;
    qint64 volumesBytes = -1;
    qint64 buildCacheBytes = -1;
    qint64 layersBytes = -1; /*!< total size of all image layers (may exceed the sum of the three) */

    int imageCount = 0;
    int containerCount = 0;
    int volumeCount = 0;
    int buildCacheCount = 0;

    /*! Sum over known categories; missing ones count as 0. */
    qint64 totalBytes() const;
};

} // namespace Kontainer

Q_DECLARE_METATYPE(Kontainer::StorageUsage)
