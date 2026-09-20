/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QDateTime>
#include <QList>
#include <QMetaType>
#include <QPair>
#include <QString>

namespace Kontainer
{

/*!
 * Docker volume (ARCH_V5_V8 §3.5).
 *
 * Read-only snapshot: `GET /volumes` already returns every field the detail page needs
 * (driver, mountpoint, labels, options, creation time), so list and detail share one
 * snapshot and `/volumes/{name}` is never fetched separately.
 *
 * `sizeBytes` and `refCount` may be unknown (`UsageData` needs a slow engine scan, can be
 * off via `--no-usage`): unknown is -1 and the UI shows "--", since "0 bytes" != "not known".
 */
struct Volume {
    QString name;
    /*! local / nfs / ... (default local). */
    QString driver;
    /*! Host mountpoint (`/var/lib/docker/volumes/<name>/_data`). */
    QString mountpoint;
    QDateTime createdAt;
    /*! local / global. */
    QString scope;
    QList<QPair<QString, QString>> labels;
    QList<QPair<QString, QString>> options;
    /*! Bytes used; -1 = not provided by the engine. */
    qint64 sizeBytes = -1;
    /*! Containers using the volume; -1 = not provided by the engine. */
    int refCount = -1;
    /*! Extra status text from the engine (data, not translated). */
    QString status;

    bool isValid() const
    {
        return !name.isEmpty();
    }
    /*! Whether a container **definitely** uses it (false when unknown; UI says "unknown", not "unused"). */
    bool isInUse() const
    {
        return refCount > 0;
    }
    /*! Whether usage is known. */
    bool usageKnown() const
    {
        return refCount >= 0;
    }
    /*! Whether the size is known. */
    bool sizeKnown() const
    {
        return sizeBytes >= 0;
    }

    bool operator==(const Volume &other) const;
};

} // namespace Kontainer

Q_DECLARE_METATYPE(Kontainer::Volume)
Q_DECLARE_METATYPE(QList<Kontainer::Volume>)
