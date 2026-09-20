/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
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
 * One-shot read-only import of the Docker CLI config (ARCH_V5_V8 §2.6/§2.7).
 *
 * It reads **someone else's file**, so boundaries matter more than features:
 * read-only, never overwrite existing wallet entries, never invoke a credential
 * helper, report bad entries instead of guessing.
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
    // Historical Docker Hub key: must normalize to index.docker.io
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

    // Keep the source key verbatim: the UI must say which ~/.docker/config.json key it came from
    QCOMPARE(byAddress.value(QStringLiteral("index.docker.io")).sourceKey, QStringLiteral("https://index.docker.io/v1/"));
    QCOMPARE(byAddress.value(QStringLiteral("index.docker.io")).credential.username, QStringLiteral("alice"));
    QCOMPARE(byAddress.value(QStringLiteral("index.docker.io")).credential.password, QStringLiteral("hub-secret"));
    // Colons inside the password must not be split
    QCOMPARE(byAddress.value(QStringLiteral("registry.example.com:5000")).credential.password, QStringLiteral("s3cret:with:colons"));
    // Token form
    QVERIFY(byAddress.value(QStringLiteral("ghcr.io")).credential.usesIdentityToken());
    QCOMPARE(byAddress.value(QStringLiteral("ghcr.io")).credential.identityToken, QStringLiteral("ci-token"));
}

void DockerCliAuthImporterTest::reportsHelperManagedEntries()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QJsonObject auths;
    // Entries managed by credsStore usually have no auth field in auths
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
    // Both helper forms must be reported, else users wonder why their registry was not imported
    QVERIFY(scan.helperManagedKeys.contains(QStringLiteral("credsStore:desktop")));
    QVERIFY(scan.helperManagedKeys.contains(QStringLiteral("credHelpers:registry.helper.test")));
    // Entries that have an auth field but broken content are listed separately
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

    // Valid JSON without auths: not an error, just nothing to import
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

    // Hub password already set in the UI: import must not revert it to the file's old value
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

    // Importing again is idempotent: nothing is written
    const DockerCliAuthImporter::ImportOutcome again = DockerCliAuthImporter::importInto(store, DockerCliAuthImporter::scan(path));
    QCOMPARE(again.imported, 0);
    QCOMPARE(again.alreadyPresent, 2);
}

/*!
 * This machine's `~/.docker/config.json` really holds `https://index.docker.io/v1/access-token`
 * and `.../refresh-token`: Docker OAuth token **caches**, not registry credentials.
 * Stripping their paths collides with the real Hub entry on one index key, and can
 * import a cached token as the password.
 */
void DockerCliAuthImporterTest::ignoresTokenCacheAndDuplicateKeys()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QJsonObject auths;
    auths.insert(QStringLiteral("https://index.docker.io/v1/"), entryWithAuth(QByteArrayLiteral("alice:real-password")));
    auths.insert(QStringLiteral("https://index.docker.io/v1/access-token"), entryWithAuth(QByteArrayLiteral("alice:access-token-cache")));
    auths.insert(QStringLiteral("https://index.docker.io/v1/refresh-token"), entryWithAuth(QByteArrayLiteral("alice:refresh-token-cache")));
    // Another spelling of the same registry: take the first, report the rest as skipped
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

    // Import writes a single entry
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
    // DOCKER_CONFIG names a directory (the CLI convention), not a file
    qputenv("DOCKER_CONFIG", "/tmp/kontainer-docker-config-test");
    QCOMPARE(DockerCliAuthImporter::defaultConfigPath(), QStringLiteral("/tmp/kontainer-docker-config-test/config.json"));
    qunsetenv("DOCKER_CONFIG");
    QVERIFY(DockerCliAuthImporter::defaultConfigPath().endsWith(QStringLiteral("/.docker/config.json")));
}

QTEST_GUILESS_MAIN(DockerCliAuthImporterTest)

#include "tst_docker_cli_auth_importer.moc"
