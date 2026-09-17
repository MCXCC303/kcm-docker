/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/docker_cli_auth_importer.h"
#include "support/fake_credential_backend.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

using namespace Kontainer;

/*!
 * Docker CLI 配置的一次性只读导入（ARCH_V5_V8 §2.6/§2.7）。
 *
 * 这个功能碰的是**别人的文件**，所以边界比功能更重要：
 * 只读、不覆盖钱包里已有的条目、不调用 credential helper、
 * 坏条目如实报出来而不是猜。
 */
class DockerCliAuthImporterTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void scansAuthsAndNormalizesIndexKeys();
    void reportsHelperManagedEntries();
    void toleratesMissingAndBrokenFiles();
    void importNeverOverwritesExistingEntries();
    void defaultPathHonoursDockerConfig();
    void ignoresTokenCacheAndDuplicateKeys();
};

namespace
{
QString writeConfig(QTemporaryDir &dir, const QJsonObject &root)
{
    const QString path = dir.path() + QStringLiteral("/config.json");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return {};
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    file.close();
    return path;
}

QJsonObject entryWithAuth(const QByteArray &userPassword)
{
    QJsonObject entry;
    entry.insert(QStringLiteral("auth"), QString::fromLatin1(userPassword.toBase64()));
    return entry;
}
} // namespace

void DockerCliAuthImporterTest::scansAuthsAndNormalizesIndexKeys()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QJsonObject auths;
    // Docker Hub 的历史键：必须归一成 index.docker.io
    auths.insert(QStringLiteral("https://index.docker.io/v1/"), entryWithAuth(QByteArrayLiteral("alice:hub-secret")));
    auths.insert(QStringLiteral("registry.example.com:5000"), entryWithAuth(QByteArrayLiteral("bob:s3cret:with:colons")));
    QJsonObject token;
    token.insert(QStringLiteral("identitytoken"), QStringLiteral("ci-token"));
    auths.insert(QStringLiteral("ghcr.io"), token);

    QJsonObject root;
    root.insert(QStringLiteral("auths"), auths);
    const QString path = writeConfig(dir, root);
    QVERIFY(!path.isEmpty());

    const DockerCliAuthScan scan = DockerCliAuthImporter::scan(path);
    QVERIFY(scan.errorKey.isEmpty());
    QCOMPARE(scan.credentials.size(), 3);

    QHash<QString, ImportableCredential> byAddress;
    for (const ImportableCredential &importable : scan.credentials) {
        byAddress.insert(importable.credential.serverAddress, importable);
    }
    QVERIFY(byAddress.contains(QStringLiteral("index.docker.io")));
    QVERIFY(byAddress.contains(QStringLiteral("registry.example.com:5000")));
    QVERIFY(byAddress.contains(QStringLiteral("ghcr.io")));

    // 来源键如实保留（界面要说明"这条来自 ~/.docker/config.json 的哪个键"）
    QCOMPARE(byAddress.value(QStringLiteral("index.docker.io")).sourceKey, QStringLiteral("https://index.docker.io/v1/"));
    QCOMPARE(byAddress.value(QStringLiteral("index.docker.io")).credential.username, QStringLiteral("alice"));
    QCOMPARE(byAddress.value(QStringLiteral("index.docker.io")).credential.password, QStringLiteral("hub-secret"));
    // 密码里的冒号不能被切断
    QCOMPARE(byAddress.value(QStringLiteral("registry.example.com:5000")).credential.password, QStringLiteral("s3cret:with:colons"));
    // 令牌形式
    QVERIFY(byAddress.value(QStringLiteral("ghcr.io")).credential.usesIdentityToken());
    QCOMPARE(byAddress.value(QStringLiteral("ghcr.io")).credential.identityToken, QStringLiteral("ci-token"));
}

void DockerCliAuthImporterTest::reportsHelperManagedEntries()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QJsonObject auths;
    // 由 credsStore 管的条目在 auths 里通常没有 auth 字段
    auths.insert(QStringLiteral("registry.helper.test"), QJsonObject());
    auths.insert(QStringLiteral("registry.broken.test"), entryWithAuth(QByteArrayLiteral("nocolon")));

    QJsonObject helpers;
    helpers.insert(QStringLiteral("registry.helper.test"), QStringLiteral("secretservice"));

    QJsonObject root;
    root.insert(QStringLiteral("auths"), auths);
    root.insert(QStringLiteral("credsStore"), QStringLiteral("desktop"));
    root.insert(QStringLiteral("credHelpers"), helpers);
    const QString path = writeConfig(dir, root);

    const DockerCliAuthScan scan = DockerCliAuthImporter::scan(path);
    QVERIFY(scan.errorKey.isEmpty());
    QVERIFY2(scan.credentials.isEmpty(), "helper-managed entries cannot be imported");
    // 助手的两种形态都要如实报告：否则用户会奇怪"我的仓库为什么没被导入"
    QVERIFY(scan.helperManagedKeys.contains(QStringLiteral("credsStore:desktop")));
    QVERIFY(scan.helperManagedKeys.contains(QStringLiteral("credHelpers:registry.helper.test")));
    // 有 auth 但内容坏掉的条目单独列出
    QCOMPARE(scan.skippedKeys, QStringList {QStringLiteral("registry.broken.test")});
}

void DockerCliAuthImporterTest::toleratesMissingAndBrokenFiles()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const DockerCliAuthScan missing = DockerCliAuthImporter::scan(dir.path() + QStringLiteral("/nope.json"));
    QCOMPARE(missing.errorKey, QStringLiteral("missingFile"));
    QVERIFY(missing.credentials.isEmpty());

    const QString broken = dir.path() + QStringLiteral("/broken.json");
    QFile file(broken);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{ not json");
    file.close();
    QCOMPARE(DockerCliAuthImporter::scan(broken).errorKey, QStringLiteral("invalidJson"));

    // 合法 JSON 但没有 auths：不是错误，只是没有可导入的东西
    QJsonObject root;
    root.insert(QStringLiteral("experimental"), QStringLiteral("enabled"));
    const QString path = writeConfig(dir, root);
    const DockerCliAuthScan empty = DockerCliAuthImporter::scan(path);
    QVERIFY(empty.errorKey.isEmpty());
    QVERIFY(empty.credentials.isEmpty());
}

void DockerCliAuthImporterTest::importNeverOverwritesExistingEntries()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QJsonObject auths;
    auths.insert(QStringLiteral("https://index.docker.io/v1/"), entryWithAuth(QByteArrayLiteral("alice:from-file")));
    auths.insert(QStringLiteral("ghcr.io"), entryWithAuth(QByteArrayLiteral("bob:from-file")));
    QJsonObject root;
    root.insert(QStringLiteral("auths"), auths);
    const QString path = writeConfig(dir, root);

    FakeCredentialBackend backend;
    CredentialStore store(&backend);
    store.open();

    // 用户已经在界面上设过 Hub 的密码：导入不得把它换回文件里的旧值
    RegistryCredential existing;
    existing.serverAddress = QStringLiteral("index.docker.io");
    existing.username = QStringLiteral("alice");
    existing.password = QStringLiteral("just-set-in-the-ui");
    QVERIFY(store.store(existing));

    const DockerCliAuthImporter::ImportOutcome outcome = DockerCliAuthImporter::importInto(store, DockerCliAuthImporter::scan(path));
    QCOMPARE(outcome.imported, 1);
    QCOMPARE(outcome.alreadyPresent, 1);
    QCOMPARE(outcome.failed, 0);
    QCOMPARE(store.credential(QStringLiteral("index.docker.io")).password, QStringLiteral("just-set-in-the-ui"));
    QCOMPARE(store.credential(QStringLiteral("ghcr.io")).password, QStringLiteral("from-file"));

    // 再导入一次：幂等，什么都不写
    const DockerCliAuthImporter::ImportOutcome again = DockerCliAuthImporter::importInto(store, DockerCliAuthImporter::scan(path));
    QCOMPARE(again.imported, 0);
    QCOMPARE(again.alreadyPresent, 2);
}

/*!
 * 本机实测 `~/.docker/config.json` 里有 `https://index.docker.io/v1/access-token`
 * 与 `.../refresh-token` 两个键：它们是 Docker 的 OAuth 令牌**缓存**，不是仓库凭据。
 * 抹掉路径后就与真正的 Hub 条目撞在同一个索引键上，甚至会把缓存令牌当密码导入。
 */
void DockerCliAuthImporterTest::ignoresTokenCacheAndDuplicateKeys()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QJsonObject auths;
    auths.insert(QStringLiteral("https://index.docker.io/v1/"), entryWithAuth(QByteArrayLiteral("alice:real-password")));
    auths.insert(QStringLiteral("https://index.docker.io/v1/access-token"), entryWithAuth(QByteArrayLiteral("alice:access-token-cache")));
    auths.insert(QStringLiteral("https://index.docker.io/v1/refresh-token"), entryWithAuth(QByteArrayLiteral("alice:refresh-token-cache")));
    // 同一仓库的另一种写法：只取第一条，剩余如实记为跳过
    auths.insert(QStringLiteral("docker.io"), entryWithAuth(QByteArrayLiteral("alice:duplicate")));

    QJsonObject root;
    root.insert(QStringLiteral("auths"), auths);
    const QString path = writeConfig(dir, root);

    const DockerCliAuthScan scan = DockerCliAuthImporter::scan(path);
    QVERIFY(scan.errorKey.isEmpty());
    QCOMPARE(scan.credentials.size(), 1);
    QCOMPARE(scan.credentials.first().credential.serverAddress, QStringLiteral("index.docker.io"));
    QCOMPARE(scan.credentials.first().credential.password, QStringLiteral("real-password"));
    QCOMPARE(scan.tokenCacheKeys.size(), 2);
    QVERIFY(scan.tokenCacheKeys.contains(QStringLiteral("https://index.docker.io/v1/access-token")));
    QVERIFY2(scan.skippedKeys.contains(QStringLiteral("docker.io")), "a duplicate address must be reported, not dropped silently");

    // 导入只写一条
    FakeCredentialBackend backend;
    CredentialStore store(&backend);
    store.open();
    const DockerCliAuthImporter::ImportOutcome outcome = DockerCliAuthImporter::importInto(store, scan);
    QCOMPARE(outcome.imported, 1);
    QCOMPARE(store.serverAddresses(), QStringList {QStringLiteral("index.docker.io")});
    QCOMPARE(store.credential(QStringLiteral("index.docker.io")).password, QStringLiteral("real-password"));
}

void DockerCliAuthImporterTest::defaultPathHonoursDockerConfig()
{
    // DOCKER_CONFIG 指的是目录（CLI 的约定），不是文件
    qputenv("DOCKER_CONFIG", "/tmp/kontainer-docker-config-test");
    QCOMPARE(DockerCliAuthImporter::defaultConfigPath(), QStringLiteral("/tmp/kontainer-docker-config-test/config.json"));
    qunsetenv("DOCKER_CONFIG");
    QVERIFY(DockerCliAuthImporter::defaultConfigPath().endsWith(QStringLiteral("/.docker/config.json")));
}

QTEST_GUILESS_MAIN(DockerCliAuthImporterTest)

#include "tst_docker_cli_auth_importer.moc"
