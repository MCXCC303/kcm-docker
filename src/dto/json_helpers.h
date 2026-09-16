/*
    SPDX-FileCopyrightText: 2026 kontainer developers
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
 * DTO 解析用的小工具（ARCH_V1 §41）：字段可能缺失、类型可能不符、未知字段直接忽略。
 * 这些函数永远不抛异常，也永远不因为可选字段异常而让整个响应失败。
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
 * Docker 的时间戳解析。
 *
 * Docker 会返回纳秒精度（9 位小数）的 RFC3339 时间，而 Qt 的 ISODateWithMs 只接受毫秒，
 * 因此这里把小数的多余位数截断；另外 Go 的零值时间（0001-01-01T00:00:00Z，表示"从未发生"）
 * 统一视为无效时间。
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
