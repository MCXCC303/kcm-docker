/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "kauth/privileged_config_request.h"

#include "backend/daemon_config.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace Kontainer
{

namespace
{
constexpr auto kRegistryMirrors = "registry-mirrors";
constexpr auto kInsecureRegistries = "insecure-registries";
constexpr auto kMaxConcurrentDownloads = "max-concurrent-downloads";
constexpr auto kLogDriver = "log-driver";
/*! Control key: list of key names to delete (not a daemon.json key). */
constexpr auto kRemove = "remove";

/*! Same shape as `Presentation::registryMirrorErrorKey`; the helper must validate on its own
 * (never trust the caller). */
bool isValidMirror(const QString &value)
{
    static const QRegularExpression pattern(QStringLiteral("^https?://[A-Za-z0-9._\\-]+(:[0-9]{1,5})?/?$"));
    return pattern.match(value.trimmed()).hasMatch();
}

bool isValidInsecureRegistry(const QString &value)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9._\\-]+(:[0-9]{1,5})?$"));
    return pattern.match(value.trimmed()).hasMatch();
}

bool parseStringList(const QVariant &value, QStringList *out)
{
    if (value.metaType().id() == QMetaType::QStringList) {
        *out = value.toStringList();
        return true;
    }
    // QML/KAuth may pass it as a QVariantList
    if (value.canConvert<QVariantList>()) {
        const QVariantList list = value.toList();
        QStringList strings;
        for (const QVariant &entry : list) {
            if (entry.metaType().id() != QMetaType::QString) {
                return false;
            }
            strings.append(entry.toString());
        }
        *out = strings;
        return true;
    }
    return false;
}
} // namespace

QStringList PrivilegedConfigRequest::allowedControlKeys()
{
    return {QStringLiteral("dryRun"), QStringLiteral("remove")};
}

QStringList PrivilegedConfigRequest::removableKeys()
{
    // Only keys we manage may be deleted: the helper must not delete what it cannot understand
    return allowedKeys();
}

QStringList PrivilegedConfigRequest::allowedKeys()
{
    return {
        QLatin1String(kRegistryMirrors),
        QLatin1String(kInsecureRegistries),
        QLatin1String(kMaxConcurrentDownloads),
        QLatin1String(kLogDriver),
    };
}

QStringList PrivilegedConfigRequest::allowedLogDrivers()
{
    // Common log drivers the daemon supports; no arbitrary strings (the helper writes a root-owned file)
    return {
        QStringLiteral("json-file"),
        QStringLiteral("local"),
        QStringLiteral("journald"),
        QStringLiteral("syslog"),
        QStringLiteral("none"),
    };
}

bool PrivilegedConfigRequest::fromArguments(const QVariantMap &arguments, PrivilegedConfigRequest *request, QString *errorKey)
{
    auto fail = [errorKey](const char *key) {
        if (errorKey) {
            *errorKey = QString::fromLatin1(key);
        }
        return false;
    };

    if (!request) {
        return fail("internalError");
    }
    PrivilegedConfigRequest parsed;

    for (auto it = arguments.constBegin(); it != arguments.constEnd(); ++it) {
        if (!allowedKeys().contains(it.key()) && !allowedControlKeys().contains(it.key())) {
            // Unknown key → reject the whole request (ignoring unknown args would look like success)
            return fail("unknownKey");
        }
    }
    parsed.m_dryRun = arguments.value(QStringLiteral("dryRun")).toBool();

    if (arguments.contains(QLatin1String(kRegistryMirrors))) {
        QStringList mirrors;
        if (!parseStringList(arguments.value(QLatin1String(kRegistryMirrors)), &mirrors)) {
            return fail("invalidValue");
        }
        if (mirrors.size() > kMaxListEntries) {
            return fail("tooLarge");
        }
        for (const QString &mirror : mirrors) {
            if (!isValidMirror(mirror)) {
                return fail("invalidValue");
            }
        }
        parsed.m_setRegistryMirrors = true;
        parsed.m_registryMirrors = mirrors;
    }

    if (arguments.contains(QLatin1String(kInsecureRegistries))) {
        QStringList registries;
        if (!parseStringList(arguments.value(QLatin1String(kInsecureRegistries)), &registries)) {
            return fail("invalidValue");
        }
        if (registries.size() > kMaxListEntries) {
            return fail("tooLarge");
        }
        for (const QString &registry : registries) {
            if (!isValidInsecureRegistry(registry)) {
                return fail("invalidValue");
            }
        }
        parsed.m_setInsecureRegistries = true;
        parsed.m_insecureRegistries = registries;
    }

    if (arguments.contains(QLatin1String(kMaxConcurrentDownloads))) {
        bool ok = false;
        const int value = arguments.value(QLatin1String(kMaxConcurrentDownloads)).toInt(&ok);
        if (!ok || value < 1 || value > 1024) {
            return fail("invalidValue");
        }
        parsed.m_setMaxConcurrentDownloads = true;
        parsed.m_maxConcurrentDownloads = value;
    }

    if (arguments.contains(QLatin1String(kLogDriver))) {
        const QString driver = arguments.value(QLatin1String(kLogDriver)).toString();
        if (!allowedLogDrivers().contains(driver)) {
            return fail("invalidValue");
        }
        parsed.m_setLogDriver = true;
        parsed.m_logDriver = driver;
    }

    if (arguments.contains(QLatin1String(kRemove))) {
        QStringList keys;
        if (!parseStringList(arguments.value(QLatin1String(kRemove)), &keys)) {
            return fail("invalidValue");
        }
        if (keys.size() > kMaxListEntries) {
            return fail("tooLarge");
        }
        for (const QString &key : keys) {
            if (!removableKeys().contains(key)) {
                // Keys outside our scope are rejected (ignoring one would look like a deletion)
                return fail("unknownKey");
            }
            // One key both assigned and marked for removal: reject instead of guessing the winner
            if ((key == QLatin1String(kMaxConcurrentDownloads) && parsed.m_setMaxConcurrentDownloads)
                || (key == QLatin1String(kLogDriver) && parsed.m_setLogDriver)
                || (key == QLatin1String(kRegistryMirrors) && parsed.m_setRegistryMirrors)
                || (key == QLatin1String(kInsecureRegistries) && parsed.m_setInsecureRegistries)) {
                return fail("conflictingKeys");
            }
        }
        parsed.m_removeKeys = keys;
    }

    // dryRun allows an empty edit set (nothing is changed yet when the user unlocks)
    if (parsed.isEmpty() && !parsed.m_dryRun) {
        return fail("noEdits");
    }
    if (errorKey) {
        errorKey->clear();
    }
    *request = parsed;
    return true;
}

QByteArray PrivilegedConfigRequest::mergeInto(const QByteArray &existingContent) const
{
    // Use exactly the merge implementation from the UI side: keep unknown keys, touch whitelisted ones
    const QByteArray existing = existingContent.trimmed().isEmpty() ? QByteArrayLiteral("{}") : existingContent;
    const DaemonConfigDocument document = DaemonConfigDocument::fromContent(existing);
    if (!document.isValid()) {
        // Never overwrite an existing file we cannot parse (the helper obeys this too)
        return {};
    }

    DaemonConfigEdits edits;
    edits.setRegistryMirrors = m_setRegistryMirrors;
    edits.registryMirrors = m_registryMirrors;
    edits.setInsecureRegistries = m_setInsecureRegistries;
    edits.insecureRegistries = m_insecureRegistries;
    edits.concurrentDownloadsEdit = m_setMaxConcurrentDownloads ? ConfigEdit::Set : ConfigEdit::Unchanged;
    edits.maxConcurrentDownloads = m_maxConcurrentDownloads;
    edits.logDriverEdit = m_setLogDriver ? ConfigEdit::Set : ConfigEdit::Unchanged;
    edits.logDriver = m_logDriver;
    // Removal intents (every key in the remove list maps to one Remove)
    for (const QString &key : m_removeKeys) {
        if (key == QLatin1String(kMaxConcurrentDownloads)) {
            edits.concurrentDownloadsEdit = ConfigEdit::Remove;
        } else if (key == QLatin1String(kLogDriver)) {
            edits.logDriverEdit = ConfigEdit::Remove;
        } else if (key == QLatin1String(kRegistryMirrors)) {
            edits.setRegistryMirrors = true;
            edits.registryMirrors.clear();
        } else if (key == QLatin1String(kInsecureRegistries)) {
            edits.setInsecureRegistries = true;
            edits.insecureRegistries.clear();
        }
    }
    return document.merged(edits);
}

} // namespace Kontainer
