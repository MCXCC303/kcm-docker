/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/presentation.h"

#include <QClipboard>
#include <QGuiApplication>

namespace Kontainer
{

Presentation::Presentation(QObject *parent)
    : QObject(parent)
{
}

QString Presentation::stateSemanticKey(const QString &stateKey, const QString &healthKey) const
{
    // 健康状态优先于状态：Running + Unhealthy 必须看起来是“有问题”（§11.3/§12）
    if (healthKey == QLatin1String("unhealthy")) {
        return QStringLiteral("negative");
    }
    if (healthKey == QLatin1String("starting")) {
        return QStringLiteral("neutral");
    }

    if (stateKey == QLatin1String("running")) {
        return QStringLiteral("positive");
    }
    if (stateKey == QLatin1String("paused") || stateKey == QLatin1String("restarting")) {
        return QStringLiteral("neutral");
    }
    if (stateKey == QLatin1String("exited") || stateKey == QLatin1String("dead")) {
        return QStringLiteral("negative");
    }
    return QStringLiteral("disabled");
}

QString Presentation::stateIconName(const QString &stateKey) const
{
    if (stateKey == QLatin1String("running")) {
        return QStringLiteral("media-playback-start");
    }
    if (stateKey == QLatin1String("paused")) {
        return QStringLiteral("media-playback-pause");
    }
    if (stateKey == QLatin1String("restarting")) {
        return QStringLiteral("view-refresh");
    }
    if (stateKey == QLatin1String("created")) {
        return QStringLiteral("document-new");
    }
    if (stateKey == QLatin1String("exited")) {
        return QStringLiteral("media-playback-stop");
    }
    if (stateKey == QLatin1String("dead")) {
        return QStringLiteral("dialog-error");
    }
    return QStringLiteral("dialog-question");
}

QString Presentation::healthIconName(const QString &healthKey) const
{
    if (healthKey == QLatin1String("healthy")) {
        return QStringLiteral("task-complete");
    }
    if (healthKey == QLatin1String("unhealthy")) {
        return QStringLiteral("dialog-error");
    }
    if (healthKey == QLatin1String("starting")) {
        return QStringLiteral("chronometer");
    }
    return {};
}

void Presentation::copyToClipboard(const QString &text) const
{
    if (text.isEmpty()) {
        return;
    }
    // 没有 GUI 应用实例时（例如无 GUI 单元测试）直接跳过
    if (!QGuiApplication::instance()) {
        return;
    }
    if (QClipboard *clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(text);
    }
}

} // namespace Kontainer
