/*
    SPDX-FileCopyrightText: 2026 kontainer developers
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
 * 仓库凭据的编解码与索引键（ARCH_V5_V8 §2.6）。
 *
 * 这里的每一条都对应一个"看起来能用、实际会被 Docker 拒绝或索引错位"的坑：
 * URL-safe 字母表、padding、Docker Hub 的历史 serveraddress、
 * 以及 config.json 与请求头两种完全不同的 base64 内容。
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
    // 大小写不同的主机名必须落到同一个索引键，否则钱包里会出现两条互不相干的凭据
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
    // 请求头里 Hub 必须写成 Docker 的历史值，否则引擎匹配不上
    QCOMPARE(RegistryAuth::headerServerAddress(QString::fromLatin1(RegistryAuth::hubHost)), QString::fromLatin1(RegistryAuth::hubServerAddress));
    QCOMPARE(RegistryAuth::headerServerAddress(QStringLiteral("registry.example.com")), QStringLiteral("registry.example.com"));
}

void RegistryAuthTest::serverAddressFollowsTheImageReference()
{
    // 没写 registry 的引用（含 library/xxx 形式）都属于 Docker Hub
    QCOMPARE(RegistryAuth::serverAddressForImage(QStringLiteral("alpine:3.19")), QString::fromLatin1(RegistryAuth::hubHost));
    QCOMPARE(RegistryAuth::serverAddressForImage(QStringLiteral("library/alpine")), QString::fromLatin1(RegistryAuth::hubHost));
    QCOMPARE(RegistryAuth::serverAddressForImage(QStringLiteral("docker.io/library/alpine:3.19")), QString::fromLatin1(RegistryAuth::hubHost));
    QCOMPARE(RegistryAuth::serverAddressForImage(QStringLiteral("registry.example.com:5000/team/app:1.2.3")), QStringLiteral("registry.example.com:5000"));
    QCOMPARE(RegistryAuth::serverAddressForImage(QStringLiteral("ghcr.io/team/app")), QStringLiteral("ghcr.io"));
    // 非法引用不给索引键，避免拿它去查钱包
    QCOMPARE(RegistryAuth::serverAddressForImage(QStringLiteral("has space")), QString());
    QCOMPARE(RegistryAuth::serverAddressForImage(QString()), QString());
}

void RegistryAuthTest::encodeUsesUrlSafeBase64AndTheHubAddress()
{
    const QByteArray header = RegistryAuth::encode(passwordCredential());
    QVERIFY(!header.isEmpty());
    // URL-safe 字母表 + 保留 padding（与 docker CLI 一致）：不含 + /，且以 = 补足 4 的倍数
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

    // 标准 base64（config.json 里就是这种）也要能解
    const QByteArray json = QByteArrayLiteral("{\"username\":\"alice\",\"password\":\"s3cret\",\"serveraddress\":\"registry.example.com\"}");
    QCOMPARE(RegistryAuth::decode(json.toBase64()).username, QStringLiteral("alice"));
    // 去掉 padding 同样接受（有些客户端会省掉）
    QByteArray unpadded = header;
    while (unpadded.endsWith('=')) {
        unpadded.chop(1);
    }
    QCOMPARE(RegistryAuth::decode(unpadded).username, QStringLiteral("alice"));
    // 折行的 base64（配置文件里常见）不该解析失败
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

    // 合法 JSON 但没有可用凭据：不能当成"登录成功"
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
    // 索引键必须是规范化后的形式，否则和界面里选中的仓库对不上
    QCOMPARE(credential.serverAddress, QString::fromLatin1(RegistryAuth::hubHost));

    // 密码里带冒号：只按第一个冒号切分
    const RegistryCredential colons = RegistryAuth::decodeConfigAuth(QStringLiteral("ghcr.io"), QByteArrayLiteral("bob:a:b:c").toBase64());
    QCOMPARE(colons.username, QStringLiteral("bob"));
    QCOMPARE(colons.password, QStringLiteral("a:b:c"));
}

void RegistryAuthTest::configAuthRejectsBrokenFields()
{
    QString errorKey;
    QVERIFY(RegistryAuth::decodeConfigAuth(QStringLiteral("ghcr.io"), QByteArray(), &errorKey).isEmpty());
    QCOMPARE(errorKey, QStringLiteral("invalidBase64"));

    // 没有冒号 → 不是 `用户名:密码`
    QVERIFY(RegistryAuth::decodeConfigAuth(QStringLiteral("ghcr.io"), QByteArrayLiteral("nocolon").toBase64(), &errorKey).isEmpty());
    QCOMPARE(errorKey, QStringLiteral("invalidAuthField"));

    // 空用户名
    QVERIFY(RegistryAuth::decodeConfigAuth(QStringLiteral("ghcr.io"), QByteArrayLiteral(":pw").toBase64(), &errorKey).isEmpty());
    QCOMPARE(errorKey, QStringLiteral("invalidAuthField"));
}

void RegistryAuthTest::encodedHeaderIsAlwaysSafeForHandWrittenHttp()
{
    // 手写 HTTP 请求：头值里出现 CR/LF 就是请求头注入。
    // 凭据来自用户输入/钱包，因此这里用恶意输入验证"编出来的头永远安全"
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
