/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QObject>
#include <QString>

namespace Kontainer
{

/*!
 * QML 侧共享的展示助手（ARCH_V2 §12/§38/§41）。
 *
 * 只回答“语义”问题（positive / neutral / negative、图标名），具体颜色仍由 QML 从
 * Kirigami.Theme 取（§12：颜色必须来自 KDE palette，不能硬编码 RGB）。
 * 这样状态颜色/图标规则只写一次，卡片、详情页、概览共用。
 */
class Presentation : public QObject
{
    Q_OBJECT

public:
    explicit Presentation(QObject *parent = nullptr);

    /*! 状态 + 健康 → 语义色 key：positive / neutral / negative / disabled。 */
    Q_INVOKABLE QString stateSemanticKey(const QString &stateKey, const QString &healthKey) const;

    /*! 容器状态 → 图标名（icon theme name）。 */
    Q_INVOKABLE QString stateIconName(const QString &stateKey) const;

    /*! 健康状态 → 图标名；无健康检查时返回空。 */
    Q_INVOKABLE QString healthIconName(const QString &healthKey) const;

    /*!
     * 复制到剪贴板（ARCH_V2 §41）。
     * 只用于 Container ID / Image ID / IP / Port 这类标识；
     * 不提供“一键复制整个 inspect JSON”。
     */
    Q_INVOKABLE void copyToClipboard(const QString &text) const;
};

} // namespace Kontainer
