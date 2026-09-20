/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
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

/*! Docker CLI token cache suffixes: `<server>/access-token`, `<server>/refresh-token`. */
const QStringList &tokenCacheSuffixes()
{
    static const QStringList suffixes = {QStringLiteral("/access-token"), QStringLiteral("/refresh-token")};
    return suffixes;
}

/*! Whether this key is Docker's own token cache rather than a registry credential. */
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
    // DOCKER_CONFIG points at a **directory** (CLI convention), not a file.
    // qEnvironmentVariable, not QProcessEnvironment: the project bans the QProcess family to keep
    // audits simple, and this is no reason to open an exception.
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
        // Log only path and error, never content (the file holds plaintext-equivalent credentials)
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

    // Helper-managed entries: known to exist, but helpers are never invoked (see the header)
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

        // Docker token cache: shares the prefix with real credentials, so filter it out first
        if (isTokenCacheKey(sourceKey)) {
            result.tokenCacheKeys.append(sourceKey);
            continue;
        }

        const QJsonObject entry = it.value().toObject();

        // Token form (common in CI): auths carries identitytoken directly
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
            // Possibly managed by credsStore/credHelpers: already recorded above, so just skip here
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

    // Deduplicate: when several keys name one registry (`docker.io` and
    // `https://index.docker.io/v1/`), keep the spelling used in request headers (Hub is
    // https://index.docker.io/v1/, others are host[:port]) and record the rest in skippedKeys.
    // Otherwise the winner drifts with JSON key order — locally `docker.io` sorted first and
    // squeezed out the real credential.
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
            // Never overwrite an existing entry: a silent overwrite loses the password the user
            // just set
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
