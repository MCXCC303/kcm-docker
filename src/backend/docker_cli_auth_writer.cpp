/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/docker_cli_auth_writer.h"

#include "backend/docker_cli_auth_importer.h"
#include "backend/registry_auth.h"
#include "logging.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace Kontainer
{

namespace
{
constexpr auto kAuths = "auths";
constexpr auto kAuth = "auth";

/*! Read the existing config; `ok` false = file exists but is unreadable/unparsable: do not write. */
QJsonObject readExisting(const QString &path, bool *ok, QString *errorKey)
{
    *ok = true;
    QFile file(path);
    if (!file.exists()) {
        return {}; // no such file yet: start from an empty config
    }
    if (!file.open(QIODevice::ReadOnly)) {
        *ok = false;
        if (errorKey) {
            *errorKey = QStringLiteral("unwritable");
        }
        qCWarning(kontainerModel) << "cannot read docker cli config:" << path << file.errorString();
        return {};
    }
    const QByteArray payload = file.readAll();
    if (payload.trimmed().isEmpty()) {
        return {};
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        // Key safety point: if the user's config is unintelligible, write **nothing** — otherwise
        // the login state stored in it is lost
        *ok = false;
        if (errorKey) {
            *errorKey = QStringLiteral("invalidJson");
        }
        qCWarning(kontainerModel) << "docker cli config is not valid json; refusing to touch it:" << path;
        return {};
    }
    return document.object();
}

/*! Atomic write (QSaveFile) with 0600: the file holds plaintext-equivalent credentials. */
bool writeConfig(const QString &path, const QJsonObject &root, QString *errorKey)
{
    const QDir dir = QFileInfo(path).absoluteDir();
    if (!dir.exists() && !QDir().mkpath(dir.absolutePath())) {
        if (errorKey) {
            *errorKey = QStringLiteral("unwritable");
        }
        return false;
    }
    // Directory 0700 (leave it alone if it already exists, so user permissions are not changed)
    QFile::setPermissions(dir.absolutePath(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorKey) {
            *errorKey = QStringLiteral("unwritable");
        }
        qCWarning(kontainerModel) << "cannot write docker cli config:" << path << file.errorString();
        return false;
    }
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (errorKey) {
            *errorKey = QStringLiteral("unwritable");
        }
        qCWarning(kontainerModel) << "cannot commit docker cli config:" << path << file.errorString();
        return false;
    }
    return true;
}

/*!
 * Whether two addresses point at the same registry.
 *
 * Must use `normalizeServerAddress()` (it handles **registry addresses**);
 * `serverAddressForImage()` is for image references and lumps anything unrecognized into Docker
 * Hub — comparing with it writes a new registry's credentials into the Hub entry (hit in testing).
 */
bool sameRegistry(const QString &lhs, const QString &rhs)
{
    const QString left = RegistryAuth::normalizeServerAddress(lhs);
    const QString right = RegistryAuth::normalizeServerAddress(rhs);
    return !left.isEmpty() && left == right;
}
} // namespace

QString DockerCliAuthWriter::defaultConfigPath()
{
    return DockerCliAuthImporter::defaultConfigPath();
}

QString DockerCliAuthWriter::configKeyFor(const QString &path, const QString &serverAddress)
{
    bool ok = false;
    const QJsonObject existing = readExisting(path.isEmpty() ? defaultConfigPath() : path, &ok, nullptr);
    if (ok) {
        const QJsonObject auths = existing.value(QString::fromLatin1(kAuths)).toObject();
        for (auto it = auths.constBegin(); it != auths.constEnd(); ++it) {
            // Reuse the spelling already in the file so one registry does not get two keys
            if (sameRegistry(it.key(), serverAddress)) {
                return it.key();
            }
        }
    }
    return serverAddress;
}

bool DockerCliAuthWriter::upsert(const QString &path,
                                 const QString &serverAddress,
                                 const RegistryCredential &credential,
                                 QString *errorKey)
{
    if (serverAddress.isEmpty() || credential.username.isEmpty()) {
        if (errorKey) {
            *errorKey = QStringLiteral("noCredentials");
        }
        return false;
    }
    const QString configPath = path.isEmpty() ? defaultConfigPath() : path;

    bool ok = false;
    QJsonObject root = readExisting(configPath, &ok, errorKey);
    if (!ok) {
        return false;
    }

    QJsonObject auths = root.value(QString::fromLatin1(kAuths)).toObject();
    const QString key = configKeyFor(configPath, serverAddress);
    QJsonObject entry = auths.value(key).toObject(); // keep existing fields such as identitytoken
    /*
     * Secret comes from password; for token logins from identityToken (the CLI's `docker login`
     * also writes the token as the `auth` password). Both empty means an incomplete credential,
     * already rejected above.
     */
    const QString secret = !credential.password.isEmpty() ? credential.password : credential.identityToken;
    entry.insert(QString::fromLatin1(kAuth), RegistryAuth::encodeConfigAuth(credential.username, secret));
    auths.insert(key, entry);
    root.insert(QString::fromLatin1(kAuths), auths);

    return writeConfig(configPath, root, errorKey);
}

bool DockerCliAuthWriter::remove(const QString &path, const QString &serverAddress, QString *errorKey)
{
    const QString configPath = path.isEmpty() ? defaultConfigPath() : path;

    bool ok = false;
    QJsonObject root = readExisting(configPath, &ok, errorKey);
    if (!ok) {
        return false;
    }

    QJsonObject auths = root.value(QString::fromLatin1(kAuths)).toObject();
    QStringList toRemove;
    for (auto it = auths.constBegin(); it != auths.constEnd(); ++it) {
        if (sameRegistry(it.key(), serverAddress)) {
            toRemove.append(it.key());
        }
    }
    if (toRemove.isEmpty()) {
        return true; // nothing there: not a failure
    }
    for (const QString &key : toRemove) {
        auths.remove(key);
    }
    root.insert(QString::fromLatin1(kAuths), auths);
    return writeConfig(configPath, root, errorKey);
}

} // namespace Kontainer
