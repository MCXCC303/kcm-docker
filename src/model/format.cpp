/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/format.h"

#include <KFormat>
#include <KLocalizedString>

#include <QDateTime>
#include <QLocale>

namespace Kontainer
{

Format::Format(QObject *parent)
    : QObject(parent)
{
}

bool Format::isValid(const QDateTime &dateTime) const
{
    return dateTime.isValid();
}

QString Format::elapsed(const QDateTime &dateTime) const
{
    if (!dateTime.isValid()) {
        return {};
    }
    const qint64 msecs = dateTime.toUTC().msecsTo(QDateTime::currentDateTimeUtc());
    if (msecs < 0) {
        // 时钟偏差等情况：不展示负数时长
        return i18nc("@info elapsed time just now", "less than a minute");
    }
    if (msecs < 60 * 1000) {
        return i18nc("@info elapsed time less than one minute", "less than a minute");
    }
    // 本地化的自然语言时长，例如 "1 day and 1 hour"（§39：格式化属于 presentation layer）
    return KFormat().formatSpelloutDuration(quint64(msecs));
}

QString Format::absoluteTime(const QDateTime &dateTime) const
{
    if (!dateTime.isValid()) {
        return {};
    }
    return QLocale().toString(dateTime.toLocalTime(), QLocale::ShortFormat);
}

QString Format::byteSize(qint64 bytes) const
{
    if (bytes < 0) {
        return {};
    }
    return KFormat().formatByteSize(double(bytes));
}

} // namespace Kontainer
