/*
    SPDX-FileCopyrightText: 2026 kontainer developers
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

/*! 去掉所有空白：base64 里出现的换行/空格（配置文件里可能折行）不该导致解析失败。 */
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

/*! 按"看起来像哪种字母表"选择解码方式：`-`/`_` 是 URL-safe，`+`/`/` 是标准。 */
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
    // 另一种字母表再试一次：纯字母数字的输入两种都能解，但混入 `=` 之外的符号时只有一种可行
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

    // 协议前缀：Docker 的配置里可能写 `https://host/v1/`，也可能只写 `host`
    const QRegularExpression scheme(QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]*://"));
    address.remove(scheme);

    // 去掉路径、查询与末尾斜杠：凭据的索引只认 host[:port]
    const int slash = address.indexOf(QLatin1Char('/'));
    if (slash >= 0) {
        address = address.left(slash);
    }
    while (address.endsWith(QLatin1Char('/'))) {
        address.chop(1);
    }
    address = address.toLower();

    // Docker Hub 的几种写法归一到同一个键（Docker 自己也是这么做的）
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
        return QString::fromLatin1(hubHost); // 没写 registry 就是 Docker Hub
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
    // URL-safe + 保留 padding：与 docker CLI 的 base64.URLEncoding 一致
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
    // 标准 base64（不是 base64url）：`~/.docker/config.json` 的 `auths` 用这种
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

    // 约定：`用户名:密码`，密码里可以再出现冒号（只按第一个冒号切分）
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
