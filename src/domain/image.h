/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QStringList>

namespace Kontainer
{

/*!
 * Image domain object.
 *
 * Phase one extended `GET /images/json` (a read-only endpoint beyond the ARCH_V1 §7.1 allowlist)
 * as the user confirmed; it stays read-only and involves no pull/push/remove.
 */
class Image
{
public:
    QString id;
    QStringList repoTags;
    QStringList repoDigests;
    qint64 sizeBytes = 0;
    QDateTime created; /*!< UTC */
    int containerCount = -1; /*!< -1 = engine did not provide it (or it is unused) */
    bool inUse = false;

    bool isValid() const
    {
        return !id.isEmpty();
    }
    /*! 12-character short ID with the "sha256:" prefix removed. */
    QString shortId() const;
    /*! First repo tag; empty when there is none (dangling). */
    QString primaryTag() const;
    bool isDangling() const;

    /*! Value comparison: unchanged data must not reset the model (ARCH_V2 §32/§34). */
    friend bool operator==(const Image &lhs, const Image &rhs)
    {
        return lhs.id == rhs.id && lhs.repoTags == rhs.repoTags && lhs.repoDigests == rhs.repoDigests && lhs.sizeBytes == rhs.sizeBytes
            && lhs.created == rhs.created && lhs.containerCount == rhs.containerCount && lhs.inUse == rhs.inUse;
    }
};

} // namespace Kontainer

Q_DECLARE_METATYPE(Kontainer::Image)
