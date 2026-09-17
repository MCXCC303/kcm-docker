/*
    SPDX-FileCopyrightText: 2026 kontainer developers
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

/*! 与界面侧 `Presentation::registryMirrorErrorKey` 相同的形态；helper 侧必须独立校验（不信任调用方）。 */
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
    // QML/KAuth 可能把它传成 QVariantList
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
    // daemon 支持的常见日志驱动；不接受任意字符串（helper 写的是 root 拥有的文件）
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
        if (!allowedKeys().contains(it.key())) {
            // 未知键 → 整请求拒绝（"忽略未知参数"会让调用方以为生效了）
            return fail("unknownKey");
        }
    }

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
        parsed.m_maxConcurrentDownloads = value;
    }

    if (arguments.contains(QLatin1String(kLogDriver))) {
        const QString driver = arguments.value(QLatin1String(kLogDriver)).toString();
        if (!allowedLogDrivers().contains(driver)) {
            return fail("invalidValue");
        }
        parsed.m_logDriver = driver;
    }

    if (parsed.isEmpty()) {
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
    // 用与界面侧完全相同的合并实现：未知键保留、只动白名单键
    const QByteArray existing = existingContent.trimmed().isEmpty() ? QByteArrayLiteral("{}") : existingContent;
    const DaemonConfigDocument document = DaemonConfigDocument::fromContent(existing);
    if (!document.isValid()) {
        // 看不懂的既有文件绝不覆写（helper 侧同样遵守）
        return {};
    }

    DaemonConfigEdits edits;
    edits.setRegistryMirrors = m_setRegistryMirrors;
    edits.registryMirrors = m_registryMirrors;
    edits.setInsecureRegistries = m_setInsecureRegistries;
    edits.insecureRegistries = m_insecureRegistries;
    edits.maxConcurrentDownloads = m_maxConcurrentDownloads;
    edits.logDriver = m_logDriver;
    return document.merged(edits);
}

} // namespace Kontainer
