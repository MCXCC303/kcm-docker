/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/credential_store.h"
#include "backend/kwallet_credential_store.h"
#include "support/fake_credential_backend.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest>

using namespace Kontainer;

/*!
 * Credential store (ARCH_V5_V8 §2.6).
 *
 * Covers three error-prone areas:
 *   1. **Index key**: `https://Index.Docker.io/v1/`, `docker.io` and
 *      `registry-1.docker.io` all name one registry and must be one entry;
 *   2. **Entry format**: passwords may contain colons/quotes/newlines and must
 *      come back verbatim (JSON, not string concatenation);
 *   3. **Availability**: a disabled wallet or a user refusing to unlock is a
 *      normal path and must fail explicitly instead of silently losing data.
 *
 * The wallet itself uses an in-memory backend double; `KWalletBackend` only
 * asserts wallet-free facts such as unavailable before open. Real wallet I/O
 * lives in the manual acceptance checklist (no desktop / no wallet = no run).
 */
class CredentialStoreTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void storesAndReadsCredentials();
    void indexKeyIsNormalizedOnEveryPath();
    void storesIdentityTokensAsWell();
    void damagedEntriesAreReportedAsMissing();
    void unavailableWalletIsAnExplicitFailure();
    void writeFailuresAreReported();
    void openingIsIdempotent();
    void asynchronousOpenIsHonoured();
    void removingUnknownEntryIsNotAnError();
    void kwalletBackendNeedsAnOpenWallet();
};

namespace
{
RegistryCredential credentialFor(const QString &serverAddress, const QString &user = QStringLiteral("alice"))
{
    RegistryCredential credential;
    credential.serverAddress = serverAddress;
    credential.username = user;
    credential.password = QStringLiteral("s3cret");
    return credential;
}
} // namespace

void CredentialStoreTest::storesAndReadsCredentials()
{
    FakeCredentialBackend backend;
    CredentialStore store(&backend);
    QSignalSpy changed(&store, &CredentialStore::changed);
    store.open();
    QCOMPARE(store.state(), CredentialStore::State::Ready);

    QString errorKey;
    QVERIFY(store.store(credentialFor(QStringLiteral("registry.example.com:5000")), &errorKey));
    QVERIFY(errorKey.isEmpty());
    QCOMPARE(changed.count(), 2); // open (Ready) + write

    QCOMPARE(store.serverAddresses(), QStringList {QStringLiteral("registry.example.com:5000")});
    QVERIFY(store.hasCredential(QStringLiteral("registry.example.com:5000")));
    const RegistryCredential loaded = store.credential(QStringLiteral("registry.example.com:5000"));
    QCOMPARE(loaded.username, QStringLiteral("alice"));
    QCOMPARE(loaded.password, QStringLiteral("s3cret"));
    QCOMPARE(loaded.serverAddress, QStringLiteral("registry.example.com:5000"));

    // Overwrite: a second save for the same registry must replace, not add
    QVERIFY(store.store(credentialFor(QStringLiteral("registry.example.com:5000"), QStringLiteral("bob"))));
    QCOMPARE(store.serverAddresses().size(), 1);
    QCOMPARE(store.credential(QStringLiteral("registry.example.com:5000")).username, QStringLiteral("bob"));

    QVERIFY(store.remove(QStringLiteral("registry.example.com:5000")));
    QVERIFY(!store.hasCredential(QStringLiteral("registry.example.com:5000")));
    QVERIFY(store.serverAddresses().isEmpty());
}

void CredentialStoreTest::indexKeyIsNormalizedOnEveryPath()
{
    FakeCredentialBackend backend;
    CredentialStore store(&backend);
    store.open();

    // Store it under the legacy config.json spelling
    QVERIFY(store.store(credentialFor(QStringLiteral("https://index.docker.io/v1/"))));
    // docker.io / registry-1.docker.io / index.docker.io all find the same entry
    for (const char *alias : {"docker.io", "registry-1.docker.io", "index.docker.io", "https://INDEX.docker.io"}) {
        QVERIFY2(store.hasCredential(QString::fromLatin1(alias)), alias);
        QCOMPARE(store.credential(QString::fromLatin1(alias)).username, QStringLiteral("alice"));
    }
    QCOMPARE(store.serverAddresses(), QStringList {QStringLiteral("index.docker.io")});

    // Host case and trailing slash differ too, still one entry
    QVERIFY(store.store(credentialFor(QStringLiteral("https://Registry.Example.com/"))));
    QVERIFY(store.store(credentialFor(QStringLiteral("registry.example.com"), QStringLiteral("bob"))));
    QCOMPARE(store.serverAddresses(), QStringList({QStringLiteral("index.docker.io"), QStringLiteral("registry.example.com")}));
    QCOMPARE(store.credential(QStringLiteral("REGISTRY.example.com")).username, QStringLiteral("bob"));
}

void CredentialStoreTest::storesIdentityTokensAsWell()
{
    FakeCredentialBackend backend;
    CredentialStore store(&backend);
    store.open();

    RegistryCredential token;
    token.serverAddress = QStringLiteral("ghcr.io");
    token.identityToken = QStringLiteral("token-123");
    QVERIFY(store.store(token));

    const RegistryCredential loaded = store.credential(QStringLiteral("ghcr.io"));
    QVERIFY(loaded.usesIdentityToken());
    QCOMPARE(loaded.identityToken, QStringLiteral("token-123"));
    QVERIFY2(loaded.username.isEmpty() && loaded.password.isEmpty(), "a token login must not invent a username");
}

void CredentialStoreTest::damagedEntriesAreReportedAsMissing()
{
    FakeCredentialBackend backend;
    CredentialStore store(&backend);
    store.open();

    // Entries damaged by old versions or hand editing: no half credential, no crash
    backend.entries.insert(QStringLiteral("broken.example.com"), QByteArrayLiteral("{not json"));
    backend.entries.insert(QStringLiteral("empty.example.com"), QByteArrayLiteral("{}"));
    for (const char *address : {"broken.example.com", "empty.example.com"}) {
        const RegistryCredential loaded = store.credential(QString::fromLatin1(address));
        QVERIFY2(loaded.isEmpty(), address);
    }
    // Existence is still reported truthfully (entry present, just unusable), so the UI can prompt a re-login
    QVERIFY(store.hasCredential(QStringLiteral("broken.example.com")));
}

void CredentialStoreTest::unavailableWalletIsAnExplicitFailure()
{
    FakeCredentialBackend backend;
    backend.enabled = false; // user turned the wallet subsystem off
    CredentialStore store(&backend);
    QSignalSpy stateSpy(&store, &CredentialStore::stateChanged);
    store.open();

    QCOMPARE(store.state(), CredentialStore::State::Unavailable);
    QCOMPARE(store.unavailableReason(), QStringLiteral("walletDisabled"));
    QCOMPARE(stateSpy.count(), 2); // Closed → Opening → Unavailable

    QString errorKey;
    QVERIFY2(!store.store(credentialFor(QStringLiteral("registry.example.com")), &errorKey), "no wallet, no write");
    QCOMPARE(errorKey, QStringLiteral("unavailable"));
    QVERIFY(!store.remove(QStringLiteral("registry.example.com"), &errorKey));
    QCOMPARE(errorKey, QStringLiteral("unavailable"));
    QVERIFY(store.serverAddresses().isEmpty());
    QVERIFY(!store.hasCredential(QStringLiteral("registry.example.com")));
}

void CredentialStoreTest::writeFailuresAreReported()
{
    FakeCredentialBackend backend;
    CredentialStore store(&backend);
    store.open();
    backend.writeFails = true;

    QString errorKey;
    QVERIFY(!store.store(credentialFor(QStringLiteral("registry.example.com")), &errorKey));
    QCOMPARE(errorKey, QStringLiteral("writeFailed"));

    // Reject bad arguments before touching the wallet.
    QVERIFY(!store.store(credentialFor(QString()), &errorKey));
    QCOMPARE(errorKey, QStringLiteral("invalidServerAddress"));
    RegistryCredential nameless;
    nameless.serverAddress = QStringLiteral("registry.example.com");
    QVERIFY(!store.store(nameless, &errorKey));
    QCOMPARE(errorKey, QStringLiteral("noCredentials"));

    // Username without password does not count: that would store an entry that looks usable but is not
    RegistryCredential noPassword;
    noPassword.serverAddress = QStringLiteral("registry.example.com");
    noPassword.username = QStringLiteral("alice");
    QVERIFY(!store.store(noPassword, &errorKey));
    QCOMPARE(errorKey, QStringLiteral("noCredentials"));
    QVERIFY(!store.remove(QString(), &errorKey));
    QCOMPARE(errorKey, QStringLiteral("invalidServerAddress"));
}

void CredentialStoreTest::openingIsIdempotent()
{
    FakeCredentialBackend backend;
    CredentialStore store(&backend);
    store.open();
    store.open();
    store.open();
    QCOMPARE(store.state(), CredentialStore::State::Ready);
    QCOMPARE(backend.openRequests, 1);
}

void CredentialStoreTest::asynchronousOpenIsHonoured()
{
    FakeCredentialBackend backend;
    backend.synchronousOpen = false; // like KWallet: wait for the user to unlock
    CredentialStore store(&backend);
    QSignalSpy stateSpy(&store, &CredentialStore::stateChanged);
    store.open();
    QCOMPARE(store.state(), CredentialStore::State::Opening);
    QVERIFY2(store.serverAddresses().isEmpty(), "nothing may be read before the wallet is open");

    backend.completeOpen();
    QCOMPARE(store.state(), CredentialStore::State::Ready);
    QCOMPARE(stateSpy.count(), 2);
}

void CredentialStoreTest::removingUnknownEntryIsNotAnError()
{
    FakeCredentialBackend backend;
    CredentialStore store(&backend);
    store.open();
    QSignalSpy changed(&store, &CredentialStore::changed);
    QString errorKey;
    QVERIFY2(store.remove(QStringLiteral("never-stored.example.com"), &errorKey), "removing nothing must succeed");
    QVERIFY(errorKey.isEmpty());
    // Removing nothing must not emit changed(), or the UI needlessly rebuilds the list
    QCOMPARE(changed.count(), 0);
}

void CredentialStoreTest::kwalletBackendNeedsAnOpenWallet()
{
    // Only assert facts that don't need the wallet daemon, e.g. unavailable before open:
    // a real open pops an unlock dialog, which no automated test may trigger (manual checklist covers it)
    KWalletBackend backend;
    QVERIFY2(!backend.isAvailable(), "the wallet is not open before open() is called");
    QCOMPARE(KWalletBackend::folderName(), QStringLiteral("Kontainer"));
    QVERIFY2(!KWalletBackend::walletName().isEmpty(), "the network wallet name must be resolvable");
}

QTEST_GUILESS_MAIN(CredentialStoreTest)

#include "tst_credential_store.moc"
