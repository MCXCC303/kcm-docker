/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/docker_cli_auth_importer.h"

#include "logging.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include <algorithm>

namespace Kontainer
{

namespace
{
constexpr auto kAuths = "auths";
constexpr auto kAuth = "auth";
constexpr auto kIdentityToken = "identitytoken";
constexpr auto kCredsStore = "credsStore";
constexpr auto kCredHelpers = "credHelpers";

/*! Docker CLI 的令牌缓存后缀：`<server>/access-token`、`<server>/refresh-token`。 */
const QStringList &tokenCacheSuffixes()
{
    static const QStringList suffixes = {QStringLiteral("/access-token"), QStringLiteral("/refresh-token")};
    return suffixes;
}

/*! 这个键是不是 Docker 自己的令牌缓存（不是仓库凭据）。 */
bool isTokenCacheKey(const QString &key)
{
    for (const QString &suffix : tokenCacheSuffixes()) {
        if (key.endsWith(suffix)) {
            return true;
        }
    }
    return false;
}
} // namespace

QString DockerCliAuthImporter::defaultConfigPath()
{
    // DOCKER_CONFIG 指向的是**目录**（CLI 的约定），不是文件
    // 用 qEnvironmentVariable 而不是 QProcessEnvironment：项目有一条"绝不 shell out"的
    // 源码约定（禁止 QProcess 家族），不为了让审计保持简单而开口子
    const QString configured = qEnvironmentVariable("DOCKER_CONFIG");
    if (!configured.isEmpty()) {
        QDir dir(configured);
        if (dir.isRelative()) {
            dir = QDir(QDir::homePath() + QLatin1Char('/') + configured);
        }
        return dir.filePath(QStringLiteral("config.json"));
    }
    return QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + QStringLiteral("/.docker/config.json");
}

DockerCliAuthScan DockerCliAuthImporter::scan(const QString &path)
{
    DockerCliAuthScan result;
    result.path = path.isEmpty() ? defaultConfigPath() : path;

    const QFileInfo info(result.path);
    if (!info.exists()) {
        result.errorKey = QStringLiteral("missingFile");
        return result;
    }

    QFile file(result.path);
    if (!file.open(QIODevice::ReadOnly)) {
        // 只记路径与错误，不记内容（文件里有等价明文的凭据）
        qCWarning(kontainerModel) << "cannot read docker cli config:" << result.path << file.errorString();
        result.errorKey = QStringLiteral("unreadable");
        return result;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.errorKey = QStringLiteral("invalidJson");
        return result;
    }

    const QJsonObject root = document.object();

    // 凭据助手管理的条目：我们知道它们存在，但不调用助手（见头文件说明）
    const QString credsStore = root.value(QLatin1String(kCredsStore)).toString();
    const QJsonObject credHelpers = root.value(QLatin1String(kCredHelpers)).toObject();
    if (!credsStore.isEmpty()) {
        result.helperManagedKeys.append(QStringLiteral("credsStore:%1").arg(credsStore));
    }
    for (auto it = credHelpers.constBegin(); it != credHelpers.constEnd(); ++it) {
        result.helperManagedKeys.append(QStringLiteral("credHelpers:%1").arg(it.key()));
    }

    QList<ImportableCredential> parsed;

    const QJsonObject auths = root.value(QLatin1String(kAuths)).toObject();
    for (auto it = auths.constBegin(); it != auths.constEnd(); ++it) {
        const QString sourceKey = it.key();

        // Docker 的令牌缓存：与真正的凭据共用一个前缀，先挑出来忽略掉
        if (isTokenCacheKey(sourceKey)) {
            result.tokenCacheKeys.append(sourceKey);
            continue;
        }

        const QJsonObject entry = it.value().toObject();

        // 令牌形式（CI 常见）：auths 里直接给 identitytoken
        const QString identityToken = entry.value(QLatin1String(kIdentityToken)).toString();
        if (!identityToken.isEmpty()) {
            ImportableCredential importable;
            importable.sourceKey = sourceKey;
            importable.credential.serverAddress = RegistryAuth::normalizeServerAddress(sourceKey);
            importable.credential.identityToken = identityToken;
            if (!importable.credential.isEmpty()) {
                parsed.append(importable);
            } else {
                result.skippedKeys.append(sourceKey);
            }
            continue;
        }

        const QByteArray auth = entry.value(QLatin1String(kAuth)).toString().toLatin1();
        if (auth.isEmpty()) {
            // 可能是由 credsStore/credHelpers 管的条目：已经在上面登记过，这里只记"跳过"
            if (result.helperManagedKeys.isEmpty()) {
                result.skippedKeys.append(sourceKey);
            }
            continue;
        }

        QString errorKey;
        RegistryCredential credential = RegistryAuth::decodeConfigAuth(sourceKey, auth, &errorKey);
        if (credential.isEmpty()) {
            qCWarning(kontainerModel) << "docker cli config entry is unusable:" << sourceKey << errorKey;
            result.skippedKeys.append(sourceKey);
            continue;
        }

        ImportableCredential importable;
        importable.sourceKey = sourceKey;
        importable.credential = credential;
        parsed.append(importable);
    }

    // 去重：多个键指向同一个仓库时（`docker.io` 与 `https://index.docker.io/v1/`），
    // 保留"请求头里该用的那个写法"（Hub 是 https://index.docker.io/v1/，其余是 host[:port]），
    // 其余如实记入 skippedKeys。不这么做的话结果会随 JSON 键的排序漂移——
    // 本机实测里 `docker.io` 恰好排在前面，会把真正的凭据挤掉。
    for (const ImportableCredential &candidate : parsed) {
        const QString canonicalKey = RegistryAuth::headerServerAddress(candidate.credential.serverAddress);
        auto existing = std::find_if(result.credentials.begin(), result.credentials.end(),
                                     [&candidate](const ImportableCredential &entry) {
                                         return entry.credential.serverAddress == candidate.credential.serverAddress;
                                     });
        if (existing == result.credentials.end()) {
            result.credentials.append(candidate);
            continue;
        }
        if (existing->sourceKey != canonicalKey && candidate.sourceKey == canonicalKey) {
            result.skippedKeys.append(existing->sourceKey);
            *existing = candidate;
        } else {
            result.skippedKeys.append(candidate.sourceKey);
        }
    }

    return result;
}

DockerCliAuthImporter::ImportOutcome DockerCliAuthImporter::importInto(CredentialStore &store, const DockerCliAuthScan &scan)
{
    ImportOutcome outcome;
    for (const ImportableCredential &importable : scan.credentials) {
        if (store.hasCredential(importable.credential.serverAddress)) {
            // 已有条目一律不覆盖：静默覆盖会让用户丢掉刚设好的密码
            ++outcome.alreadyPresent;
            continue;
        }
        if (store.store(importable.credential)) {
            ++outcome.imported;
        } else {
            ++outcome.failed;
        }
    }
    return outcome;
}

} // namespace Kontainer
