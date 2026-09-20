/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/registry_auth.h"

#include "domain/image_reference.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>

namespace Kontainer
{

namespace
{
constexpr auto kUsername = "username";
constexpr auto kPassword = "password";
constexpr auto kServerAddress = "serveraddress";
constexpr auto kIdentityToken = "identitytoken";

/*! Strip whitespace: newlines/spaces in base64 (config files may wrap) must not break decoding. */
QByteArray withoutWhitespace(const QByteArray &value)
{
    QByteArray out;
    out.reserve(value.size());
    for (const char ch : value) {
        if (ch != ' ' && ch != '\n' && ch != '\r' && ch != '\t') {
            out.append(ch);
        }
    }
    return out;
}

/*! Pick the alphabet by what the input looks like: `-`/`_` means URL-safe, `+`/`/` standard. */
QByteArray decodeBase64Loose(const QByteArray &value)
{
    const QByteArray trimmed = withoutWhitespace(value);
    if (trimmed.isEmpty()) {
        return {};
    }
    const bool looksUrlSafe = trimmed.contains('-') || trimmed.contains('_');
    const QByteArray::Base64Options options = (looksUrlSafe ? QByteArray::Base64UrlEncoding : QByteArray::Base64Encoding)
        | QByteArray::AbortOnBase64DecodingErrors;

    QByteArray decoded = QByteArray::fromBase64(trimmed, options);
    if (!decoded.isEmpty()) {
        return decoded;
    }
    // Retry with the other alphabet: pure alphanumeric input works either way, other symbols do not
    return QByteArray::fromBase64(trimmed,
                                  (looksUrlSafe ? QByteArray::Base64Encoding : QByteArray::Base64UrlEncoding)
                                      | QByteArray::AbortOnBase64DecodingErrors);
}
} // namespace

QString RegistryAuth::normalizeServerAddress(const QString &value)
{
    QString address = value.trimmed();
    if (address.isEmpty()) {
        return {};
    }

    // Scheme prefix: Docker configs may write `https://host/v1/` or just `host`
    const QRegularExpression scheme(QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]*://"));
    address.remove(scheme);

    // Drop path, query and trailing slashes: the credential index only knows host[:port]
    const int slash = address.indexOf(QLatin1Char('/'));
    if (slash >= 0) {
        address = address.left(slash);
    }
    while (address.endsWith(QLatin1Char('/'))) {
        address.chop(1);
    }
    address = address.toLower();

    // Collapse all Docker Hub spellings to one key (Docker itself does the same)
    if (address == QLatin1String("docker.io") || address == QLatin1String("index.docker.io")
        || address == QLatin1String("registry-1.docker.io") || address == QLatin1String("registry.docker.io")
        || address == QLatin1String("hub.docker.com")) {
        return QString::fromLatin1(hubHost);
    }
    return address;
}

QString RegistryAuth::headerServerAddress(const QString &normalizedServerAddress)
{
    if (normalizedServerAddress == QLatin1String(hubHost)) {
        return QString::fromLatin1(hubServerAddress);
    }
    return normalizedServerAddress;
}

QString RegistryAuth::serverAddressForImage(const QString &imageReference)
{
    const auto parts = ImageReference::parse(imageReference);
    if (!parts) {
        return {};
    }
    if (parts->registry.isEmpty()) {
        return QString::fromLatin1(hubHost); // no registry means Docker Hub
    }
    return normalizeServerAddress(parts->registry);
}

QByteArray RegistryAuth::encode(const RegistryCredential &credential)
{
    if (credential.isEmpty()) {
        return {};
    }

    QJsonObject object;
    if (credential.usesIdentityToken()) {
        object.insert(QLatin1String(kIdentityToken), credential.identityToken);
    } else {
        object.insert(QLatin1String(kUsername), credential.username);
        object.insert(QLatin1String(kPassword), credential.password);
    }
    object.insert(QLatin1String(kServerAddress), headerServerAddress(credential.serverAddress));

    const QByteArray json = QJsonDocument(object).toJson(QJsonDocument::Compact);
    // URL-safe + kept padding: matches the docker CLI's base64.URLEncoding
    return json.toBase64(QByteArray::Base64UrlEncoding | QByteArray::KeepTrailingEquals);
}

RegistryCredential RegistryAuth::decode(const QByteArray &headerValue, QString *errorKey)
{
    auto fail = [errorKey](const char *key) {
        if (errorKey) {
            *errorKey = QString::fromLatin1(key);
        }
        return RegistryCredential();
    };

    const QByteArray trimmed = withoutWhitespace(headerValue);
    if (trimmed.isEmpty()) {
        return fail("empty");
    }

    const QByteArray json = decodeBase64Loose(trimmed);
    if (json.isEmpty()) {
        return fail("invalidBase64");
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return fail("invalidJson");
    }

    const QJsonObject object = document.object();
    RegistryCredential credential;
    credential.serverAddress = normalizeServerAddress(object.value(QLatin1String(kServerAddress)).toString());
    credential.username = object.value(QLatin1String(kUsername)).toString();
    credential.password = object.value(QLatin1String(kPassword)).toString();
    credential.identityToken = object.value(QLatin1String(kIdentityToken)).toString();

    if (credential.isEmpty()) {
        return fail("noCredentials");
    }
    if (errorKey) {
        errorKey->clear();
    }
    return credential;
}

QString RegistryAuth::encodeConfigAuth(const QString &username, const QString &password)
{
    // Standard base64 (not base64url): what `~/.docker/config.json`'s `auths` uses
    return QString::fromLatin1(QByteArray(username.toUtf8() + ':' + password.toUtf8()).toBase64());
}

RegistryCredential RegistryAuth::decodeConfigAuth(const QString &serverAddress, const QByteArray &base64UserPassword, QString *errorKey)
{
    auto fail = [errorKey](const char *key) {
        if (errorKey) {
            *errorKey = QString::fromLatin1(key);
        }
        return RegistryCredential();
    };

    const QByteArray decoded = decodeBase64Loose(base64UserPassword);
    if (decoded.isEmpty()) {
        return fail("invalidBase64");
    }

    // Convention: `user:password`; the password may contain colons (split on the first one only)
    const int separator = decoded.indexOf(':');
    if (separator <= 0) {
        return fail("invalidAuthField");
    }

    RegistryCredential credential;
    credential.serverAddress = normalizeServerAddress(serverAddress);
    credential.username = QString::fromUtf8(decoded.left(separator));
    credential.password = QString::fromUtf8(decoded.mid(separator + 1));
    if (credential.isEmpty()) {
        return fail("noCredentials");
    }
    if (errorKey) {
        errorKey->clear();
    }
    return credential;
}

} // namespace Kontainer
