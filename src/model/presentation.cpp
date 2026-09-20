/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/presentation.h"

#include "model/state_text.h"

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
    // health outranks state: Running + Unhealthy must look "wrong" (§11.3/§12)
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

QString Presentation::stateText(const QString &stateKey) const
{
    // stateKey is Docker's own state string (running/exited/…), the same mapping used when parsing DTOs
    return containerStateText(containerStateFromString(stateKey));
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
    // skip when there is no GUI application instance (e.g. headless unit tests)
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

int Presentation::connectionColorIndex(const QString &seed, int paletteSize) const
{
    if (seed.isEmpty() || paletteSize <= 0) {
        return 0;
    }
    // FNV-1a 32-bit plus a lowbias32 finalizer.
    // The finalizer is required: FNV's low bits avalanche poorly for seeds differing only in their
    // last characters, so a direct modulo crowded many seeds onto few palette slots (measured: 200
    // similar seeds used only 3 of the 6 slots).
    quint32 hash = 2166136261u;
    for (const QChar ch : seed) {
        hash ^= quint32(ch.unicode());
        hash *= 16777619u;
    }
    hash ^= hash >> 16;
    hash *= 0x7feb352du;
    hash ^= hash >> 15;
    hash *= 0x846ca68bu;
    hash ^= hash >> 16;
    return int(hash % quint32(paletteSize));
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
            continue; // skip lines that are not KEY=VALUE (pasted content is often hand-edited)
        }
        const QString key = line.left(equals).trimmed();
        QString value = line.mid(equals + 1).trimmed();
        if (!isValidEnvKey(key)) {
            continue;
        }
        // quoted: take the whole span, no escape expansion (sufficient here, and predictable)
        if (value.size() >= 2) {
            const QChar first = value.front();
            if ((first == QLatin1Char('"') || first == QLatin1Char('\'')) && value.back() == first) {
                value = value.mid(1, value.size() - 2);
            } else {
                // unquoted: strip the trailing comment (everything after ` #`)
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
        return false; // an invalid port is the range check's job, not a conflict
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

QString Presentation::registryMirrorErrorKey(const QString &value) const
{
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) {
        return QStringLiteral("emptyHost");
    }
    // a mirror is an address the daemon connects to: an http(s) URL with a host and no path/query
    static const QRegularExpression pattern(QStringLiteral("^https?://[A-Za-z0-9._\\-]+(:[0-9]{1,5})?/?$"));
    if (!pattern.match(trimmed).hasMatch()) {
        return QStringLiteral("invalid");
    }
    return {};
}

QString Presentation::insecureRegistryErrorKey(const QString &value) const
{
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) {
        return QStringLiteral("emptyHost");
    }
    // insecure-registries holds registry addresses: only host[:port], a scheme or path is wrong
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9._\\-]+(:[0-9]{1,5})?$"));
    if (!pattern.match(trimmed).hasMatch()) {
        return QStringLiteral("invalid");
    }
    return {};
}

} // namespace Kontainer
