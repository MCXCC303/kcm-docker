/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QTimeZone>

namespace Kontainer
{

/*!
 * Small helpers for DTO parsing (ARCH_V1 §41): fields may be missing or mistyped, unknown fields
 * are ignored. They never throw and never fail a whole response over one optional field.
 */
namespace JsonHelpers
{

inline QString stringValue(const QJsonObject &object, const QString &key)
{
    const QJsonValue value = object.value(key);
    return value.isString() ? value.toString() : QString();
}

inline qint64 integerValue(const QJsonObject &object, const QString &key, qint64 fallback = 0)
{
    const QJsonValue value = object.value(key);
    if (value.isDouble()) {
        return static_cast<qint64>(value.toDouble());
    }
    if (value.isString()) {
        bool ok = false;
        const qint64 parsed = value.toString().toLongLong(&ok);
        return ok ? parsed : fallback;
    }
    return fallback;
}

inline int intValue(const QJsonObject &object, const QString &key, int fallback = 0)
{
    return static_cast<int>(integerValue(object, key, fallback));
}

inline bool boolValue(const QJsonObject &object, const QString &key, bool fallback = false)
{
    const QJsonValue value = object.value(key);
    return value.isBool() ? value.toBool() : fallback;
}

/*!
 * Docker timestamp parsing.
 *
 * Docker returns RFC3339 times with nanosecond precision (9 fractional digits) while Qt's
 * ISODateWithMs accepts milliseconds only, so the extra digits are truncated; Go's zero time
 * (0001-01-01T00:00:00Z, meaning "never happened") counts as invalid.
 */
inline QDateTime parseDockerTimestamp(const QString &value)
{
    if (value.isEmpty()) {
        return {};
    }

    QString normalized = value;
    const int dot = normalized.indexOf(QLatin1Char('.'));
    if (dot > 0) {
        int stop = normalized.indexOf(QLatin1Char('Z'), dot);
        if (stop < 0) {
            stop = normalized.indexOf(QLatin1Char('+'), dot);
        }
        if (stop < 0) {
            stop = normalized.size();
        }
        if (stop - dot - 1 > 3) {
            normalized = normalized.left(dot + 4) + normalized.mid(stop);
        }
    }

    QDateTime parsed = QDateTime::fromString(normalized, Qt::ISODateWithMs);
    if (!parsed.isValid()) {
        parsed = QDateTime::fromString(normalized, Qt::ISODate);
    }
    if (!parsed.isValid() || parsed.date().year() <= 1) {
        return {};
    }
    parsed.setTimeZone(QTimeZone::UTC);
    return parsed;
}

inline QStringList stringListValue(const QJsonObject &object, const QString &key)
{
    QStringList result;
    const QJsonValue value = object.value(key);
    if (!value.isArray()) {
        return result;
    }
    const QJsonArray array = value.toArray();
    result.reserve(array.size());
    for (const QJsonValue &entry : array) {
        if (entry.isString()) {
            result.append(entry.toString());
        }
    }
    return result;
}

} // namespace JsonHelpers

} // namespace Kontainer
