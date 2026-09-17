/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/presentation.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QVariantMap>

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

bool Presentation::isValidEnvKey(const QString &key) const
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    return pattern.match(key).hasMatch();
}

QVariantList Presentation::parseEnvText(const QString &text) const
{
    QVariantList entries;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (QString line : lines) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        if (line.startsWith(QLatin1String("export "))) {
            line = line.mid(int(qstrlen("export "))).trimmed();
        }
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals <= 0) {
            continue; // 不是 KEY=VALUE 的行直接跳过（粘贴内容常常是人手整理的）
        }
        const QString key = line.left(equals).trimmed();
        QString value = line.mid(equals + 1).trimmed();
        if (!isValidEnvKey(key)) {
            continue;
        }
        // 引号包裹：整段取值，不做转义展开（够用且行为可预期）
        if (value.size() >= 2) {
            const QChar first = value.front();
            if ((first == QLatin1Char('"') || first == QLatin1Char('\'')) && value.back() == first) {
                value = value.mid(1, value.size() - 2);
            } else {
                // 未加引号：去掉行内注释（` #` 之后的内容）
                const int comment = value.indexOf(QLatin1String(" #"));
                if (comment >= 0) {
                    value = value.left(comment).trimmed();
                }
            }
        }

        QVariantMap entry;
        entry.insert(QStringLiteral("key"), key);
        entry.insert(QStringLiteral("value"), value);
        entries.append(entry);
    }
    return entries;
}

bool Presentation::isValidPort(int port) const
{
    return port >= 1 && port <= 65535;
}

bool Presentation::isWildcardHostIp(const QString &hostIp) const
{
    return hostIp.isEmpty() || hostIp == QLatin1String("0.0.0.0") || hostIp == QLatin1String("::") || hostIp == QLatin1String("[::]");
}

bool Presentation::hostPortConflicts(const QString &hostIp, int hostPort, const QStringList &usedBindings) const
{
    if (!isValidPort(hostPort)) {
        return false; // 非法端口由范围校验负责，不算冲突
    }
    const bool wildcard = isWildcardHostIp(hostIp);
    for (const QString &binding : usedBindings) {
        const int colon = binding.lastIndexOf(QLatin1Char(':'));
        if (colon <= 0) {
            continue;
        }
        bool ok = false;
        const int usedPort = binding.mid(colon + 1).toInt(&ok);
        if (!ok || usedPort != hostPort) {
            continue;
        }
        const QString usedIp = binding.left(colon);
        if (wildcard || isWildcardHostIp(usedIp) || usedIp == hostIp) {
            return true;
        }
    }
    return false;
}

} // namespace Kontainer
