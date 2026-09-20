/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/docker_client.h"
#include "backend/registry_auth.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QtTest>

using namespace Kontainer;

/*!
 * Registry credential encoding/decoding and index keys (ARCH_V5_V8 §2.6).
 *
 * Each case is a pitfall that looks usable but is rejected or misindexed by
 * Docker: URL-safe alphabet, padding, Docker Hub's historical serveraddress,
 * and the two different base64 payloads for config.json vs. the request header.
 */
class RegistryAuthTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void normalizesServerAddresses();
    void normalizesDockerHubVariants();
    void serverAddressFollowsTheImageReference();
    void encodeUsesUrlSafeBase64AndTheHubAddress();
    void encodePrefersIdentityToken();
    void decodeAcceptsBothAlphabetsAndMissingPadding();
    void decodeRejectsBrokenInput();
    void configAuthParsesUserPassword();
    void configAuthRejectsBrokenFields();
    void encodedHeaderIsAlwaysSafeForHandWrittenHttp();
};

namespace
{
RegistryCredential passwordCredential()
{
    RegistryCredential credential;
    credential.serverAddress = QStringLiteral("registry.example.com:5000");
    credential.username = QStringLiteral("alice");
    credential.password = QStringLiteral("s3cret");
    return credential;
}

QJsonObject jsonOf(const QByteArray &headerValue)
{
    return QJsonDocument::fromJson(QByteArray::fromBase64(headerValue, QByteArray::Base64UrlEncoding)).object();
}
} // namespace

void RegistryAuthTest::normalizesServerAddresses()
{
    QCOMPARE(RegistryAuth::normalizeServerAddress(QStringLiteral("  registry.example.com  ")), QStringLiteral("registry.example.com"));
    QCOMPARE(RegistryAuth::normalizeServerAddress(QStringLiteral("https://registry.example.com")), QStringLiteral("registry.example.com"));
    QCOMPARE(RegistryAuth::normalizeServerAddress(QStringLiteral("http://registry.example.com:5000/")), QStringLiteral("registry.example.com:5000"));
    QCOMPARE(RegistryAuth::normalizeServerAddress(QStringLiteral("registry.example.com/v2/")), QStringLiteral("registry.example.com"));
    QCOMPARE(RegistryAuth::normalizeServerAddress(QStringLiteral("REgistry.Example.COM")), QStringLiteral("registry.example.com"));
    QCOMPARE(RegistryAuth::normalizeServerAddress(QStringLiteral("localhost:5000")), QStringLiteral("localhost:5000"));
    QCOMPARE(RegistryAuth::normalizeServerAddress(QString()), QString());
    // Host names differing only in case must share one index key, else the wallet gets two entries
    QCOMPARE(RegistryAuth::normalizeServerAddress(QStringLiteral("https://Registry.Example.com/")),
             RegistryAuth::normalizeServerAddress(QStringLiteral("registry.example.com")));
}

void RegistryAuthTest::normalizesDockerHubVariants()
{
    for (const char *variant : {"docker.io",
                                "index.docker.io",
                                "registry-1.docker.io",
                                "registry.docker.io",
                                "https://index.docker.io/v1/",
                                "https://registry-1.docker.io",
                                "DOCKER.IO"}) {
        QCOMPARE(RegistryAuth::normalizeServerAddress(QString::fromLatin1(variant)), QString::fromLatin1(RegistryAuth::hubHost));
    }
    // Hub must be written as Docker's historical value in the header, or the engine won't match it
    QCOMPARE(RegistryAuth::headerServerAddress(QString::fromLatin1(RegistryAuth::hubHost)), QString::fromLatin1(RegistryAuth::hubServerAddress));
    QCOMPARE(RegistryAuth::headerServerAddress(QStringLiteral("registry.example.com")), QStringLiteral("registry.example.com"));
}

void RegistryAuthTest::serverAddressFollowsTheImageReference()
{
    // References without a registry (including library/xxx) all belong to Docker Hub
    QCOMPARE(RegistryAuth::serverAddressForImage(QStringLiteral("alpine:3.19")), QString::fromLatin1(RegistryAuth::hubHost));
    QCOMPARE(RegistryAuth::serverAddressForImage(QStringLiteral("library/alpine")), QString::fromLatin1(RegistryAuth::hubHost));
    QCOMPARE(RegistryAuth::serverAddressForImage(QStringLiteral("docker.io/library/alpine:3.19")), QString::fromLatin1(RegistryAuth::hubHost));
    QCOMPARE(RegistryAuth::serverAddressForImage(QStringLiteral("registry.example.com:5000/team/app:1.2.3")), QStringLiteral("registry.example.com:5000"));
    QCOMPARE(RegistryAuth::serverAddressForImage(QStringLiteral("ghcr.io/team/app")), QStringLiteral("ghcr.io"));
    // Invalid references get no index key, so they never reach the wallet
    QCOMPARE(RegistryAuth::serverAddressForImage(QStringLiteral("has space")), QString());
    QCOMPARE(RegistryAuth::serverAddressForImage(QString()), QString());
}

void RegistryAuthTest::encodeUsesUrlSafeBase64AndTheHubAddress()
{
    const QByteArray header = RegistryAuth::encode(passwordCredential());
    QVERIFY(!header.isEmpty());
    // URL-safe alphabet + kept padding (like the docker CLI): no + /, and = pads to a multiple of 4
    QVERIFY2(!header.contains('+') && !header.contains('/'), header.constData());
    QVERIFY2(header.size() % 4 == 0, header.constData());

    const QJsonObject object = jsonOf(header);
    QCOMPARE(object.value(QStringLiteral("username")).toString(), QStringLiteral("alice"));
    QCOMPARE(object.value(QStringLiteral("password")).toString(), QStringLiteral("s3cret"));
    QCOMPARE(object.value(QStringLiteral("serveraddress")).toString(), QStringLiteral("registry.example.com:5000"));

    RegistryCredential hub;
    hub.serverAddress = QString::fromLatin1(RegistryAuth::hubHost);
    hub.username = QStringLiteral("bob");
    hub.password = QStringLiteral("pw");
    QCOMPARE(jsonOf(RegistryAuth::encode(hub)).value(QStringLiteral("serveraddress")).toString(),
             QString::fromLatin1(RegistryAuth::hubServerAddress));
}

void RegistryAuthTest::encodePrefersIdentityToken()
{
    RegistryCredential credential;
    credential.serverAddress = QStringLiteral("ghcr.io");
    credential.username = QStringLiteral("ignored");
    credential.password = QStringLiteral("ignored");
    credential.identityToken = QStringLiteral("token-123");

    const QJsonObject object = jsonOf(RegistryAuth::encode(credential));
    QCOMPARE(object.value(QStringLiteral("identitytoken")).toString(), QStringLiteral("token-123"));
    QVERIFY2(!object.contains(QStringLiteral("username")), "a token login must not send username/password");
    QVERIFY2(!object.contains(QStringLiteral("password")), "a token login must not send username/password");
}

void RegistryAuthTest::decodeAcceptsBothAlphabetsAndMissingPadding()
{
    const RegistryCredential original = passwordCredential();
    const QByteArray header = RegistryAuth::encode(original);

    QString errorKey;
    RegistryCredential decoded = RegistryAuth::decode(header, &errorKey);
    QVERIFY(errorKey.isEmpty());
    QCOMPARE(decoded.username, original.username);
    QCOMPARE(decoded.password, original.password);
    QCOMPARE(decoded.serverAddress, original.serverAddress);

    // Standard base64 (what config.json holds) must decode too
    const QByteArray json = QByteArrayLiteral("{\"username\":\"alice\",\"password\":\"s3cret\",\"serveraddress\":\"registry.example.com\"}");
    QCOMPARE(RegistryAuth::decode(json.toBase64()).username, QStringLiteral("alice"));
    // Missing padding is accepted too (some clients omit it)
    QByteArray unpadded = header;
    while (unpadded.endsWith('=')) {
        unpadded.chop(1);
    }
    QCOMPARE(RegistryAuth::decode(unpadded).username, QStringLiteral("alice"));
    // Wrapped base64 (common in config files) must not fail to parse
    QByteArray wrapped = header.left(8) + "\n" + header.mid(8) + "\r\n";
    QCOMPARE(RegistryAuth::decode(wrapped).username, QStringLiteral("alice"));
}

void RegistryAuthTest::decodeRejectsBrokenInput()
{
    QString errorKey;
    QVERIFY(RegistryAuth::decode(QByteArray(), &errorKey).isEmpty());
    QCOMPARE(errorKey, QStringLiteral("empty"));

    QVERIFY(RegistryAuth::decode(QByteArrayLiteral("!!!not base64!!!"), &errorKey).isEmpty());
    QCOMPARE(errorKey, QStringLiteral("invalidBase64"));

    QVERIFY(RegistryAuth::decode(QByteArrayLiteral("bm90IGpzb24="), &errorKey).isEmpty()); // "not json"
    QCOMPARE(errorKey, QStringLiteral("invalidJson"));

    // Valid JSON but no usable credentials: must not count as a successful login
    const QByteArray empty = QByteArrayLiteral("{\"serveraddress\":\"registry.example.com\"}").toBase64();
    QVERIFY(RegistryAuth::decode(empty, &errorKey).isEmpty());
    QCOMPARE(errorKey, QStringLiteral("noCredentials"));
}

void RegistryAuthTest::configAuthParsesUserPassword()
{
    QString errorKey;
    const QByteArray auth = QByteArrayLiteral("alice:s3cret").toBase64();
    const RegistryCredential credential = RegistryAuth::decodeConfigAuth(QStringLiteral("https://index.docker.io/v1/"), auth, &errorKey);
    QVERIFY(errorKey.isEmpty());
    QCOMPARE(credential.username, QStringLiteral("alice"));
    QCOMPARE(credential.password, QStringLiteral("s3cret"));
    // Index key must be the normalized form, else it won't match the registry selected in the UI
    QCOMPARE(credential.serverAddress, QString::fromLatin1(RegistryAuth::hubHost));

    // Password contains colons: split on the first colon only
    const RegistryCredential colons = RegistryAuth::decodeConfigAuth(QStringLiteral("ghcr.io"), QByteArrayLiteral("bob:a:b:c").toBase64());
    QCOMPARE(colons.username, QStringLiteral("bob"));
    QCOMPARE(colons.password, QStringLiteral("a:b:c"));
}

void RegistryAuthTest::configAuthRejectsBrokenFields()
{
    QString errorKey;
    QVERIFY(RegistryAuth::decodeConfigAuth(QStringLiteral("ghcr.io"), QByteArray(), &errorKey).isEmpty());
    QCOMPARE(errorKey, QStringLiteral("invalidBase64"));

    // No colon → not `user:password`
    QVERIFY(RegistryAuth::decodeConfigAuth(QStringLiteral("ghcr.io"), QByteArrayLiteral("nocolon").toBase64(), &errorKey).isEmpty());
    QCOMPARE(errorKey, QStringLiteral("invalidAuthField"));

    // Empty username
    QVERIFY(RegistryAuth::decodeConfigAuth(QStringLiteral("ghcr.io"), QByteArrayLiteral(":pw").toBase64(), &errorKey).isEmpty());
    QCOMPARE(errorKey, QStringLiteral("invalidAuthField"));
}

void RegistryAuthTest::encodedHeaderIsAlwaysSafeForHandWrittenHttp()
{
    // Hand-written HTTP requests: CR/LF in a header value is header injection.
    // Credentials come from user input/the wallet, so hostile input must still encode safely
    RegistryCredential credential;
    credential.serverAddress = QStringLiteral("registry.example.com");
    credential.username = QStringLiteral("alice\r\nX-Evil: 1");
    credential.password = QStringLiteral("pw\nInjected: yes");

    const QByteArray header = RegistryAuth::encode(credential);
    QVERIFY2(!header.contains('\r') && !header.contains('\n'), header.constData());
    QVERIFY(DockerReply::isHeaderSafe(QByteArrayLiteral("X-Registry-Auth"), header));
    QVERIFY2(!DockerReply::isHeaderSafe(QByteArrayLiteral("X-Registry-Auth"), QByteArrayLiteral("a\r\nb")),
             "the client must refuse CR/LF in a header value");
    QVERIFY2(!DockerReply::isHeaderSafe(QByteArrayLiteral("X-Bad Name"), QByteArrayLiteral("v")),
             "the client must refuse a header name with a space");
}

QTEST_GUILESS_MAIN(RegistryAuthTest)

#include "tst_registry_auth.moc"
