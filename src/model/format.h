/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>

namespace Kontainer
{

/*!
 * Formatting helpers for the presentation layer (ARCH_V1 §39).
 *
 * Time and size UI strings are produced here; the domain model stores only QDateTime / byte counts and
 * the backend never produces UI strings like "Up 2 hours".
 * Registered as the QML singleton Format of org.kde.kcm.docker.
 */
class Format : public QObject
{
    Q_OBJECT

public:
    explicit Format(QObject *parent = nullptr);

    /*!
     * QDateTime becomes a JS Date in QML (no .valid property), so every validity check goes through
     * this helper.
     */
    Q_INVOKABLE bool isValid(const QDateTime &dateTime) const;
    /*! Localized elapsed time, e.g. "2 hours 5 minutes". */
    Q_INVOKABLE QString elapsed(const QDateTime &dateTime) const;
    /*! Localized absolute time. */
    Q_INVOKABLE QString absoluteTime(const QDateTime &dateTime) const;
    /*! Human-readable byte size, e.g. "6.9 GiB". */
    Q_INVOKABLE QString byteSize(qint64 bytes) const;
};

} // namespace Kontainer
