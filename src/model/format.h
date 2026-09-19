/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>

namespace Kontainer
{

/*!
 * presentation layer 的格式化工具（ARCH_V1 §39）。
 *
 * 时间与体积的 UI 文案在这里生成，domain model 只保存 QDateTime / 字节数，
 * backend 永不产出 "Up 2 hours" 这类界面字符串。
 * 作为 QML 单例注册为 org.kde.kcm.docker 的 Format。
 */
class Format : public QObject
{
    Q_OBJECT

public:
    explicit Format(QObject *parent = nullptr);

    /*!
     * QDateTime 在 QML 里会变成 JS Date（没有 .valid 属性），
     * 因此有效性判断统一走这个助手。
     */
    Q_INVOKABLE bool isValid(const QDateTime &dateTime) const;
    /*! 距今多久（本地化），例如“2 小时 5 分钟”。 */
    Q_INVOKABLE QString elapsed(const QDateTime &dateTime) const;
    /*! 本地化的绝对时间。 */
    Q_INVOKABLE QString absoluteTime(const QDateTime &dateTime) const;
    /*! 人类可读的字节数，例如 “6.9 GiB”。 */
    Q_INVOKABLE QString byteSize(qint64 bytes) const;
};

} // namespace Kontainer
