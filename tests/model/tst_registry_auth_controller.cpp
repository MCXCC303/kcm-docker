/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/credential_store.h"
#include "model/registry_auth_controller.h"
#include "model/registry_credential_model.h"
#include "support/fake_credential_backend.h"
#include "support/mock_docker_backend.h"
#include "i18n.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace Kontainer;

/*!
 * Registry auth controller (ARCH_V5_V8 §2.6/§2.7).
 *
 * These pin **rules**, not UI:
 *   1. login must pass `POST /auth` first, a failure is **never** persisted;
 *   2. an unavailable wallet only degrades (explicit error, no plaintext fallback, no fake success);
 *   3. the model holds server address and username only — passwords/tokens never enter it;
 *   4. CLI import is read-only and never overwrites, entries owned by the helper are left alone.
 */
class RegistryAuthControllerTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir *m_isolation = nullptr;

private Q_SLOTS:
    void initTestCase();
    /*!
     * Point `DOCKER_CONFIG` at an empty directory before each case.
     *
     * The cases call `refresh()` (silent CLI detection) and login/remove (**write back** the CLI config),
     * so without isolation they would read and rewrite the user's real `~/.docker/config.json`.
     */
    void init();
    void cleanup();
    void loginStoresOnlyAfterSuccessfulCheck();
    void loginRejectsIncompleteInput();
    void loginRequiresAnAvailableWallet();
    void tokenLoginIsStoredAsToken();
    void testCredentialDoesNotModifyAnything();
    void removeDropsTheCredential();
    void modelNeverExposesSecrets();
    void cliEntriesAreRecognizedSilently();
    void changesAreWrittenBackToTheCliConfig();
    void walletLifecycleIsReportedToTheUi();
};

namespace
{
using AuthResult = DockerBackendInterface::AuthCheckResult;

QString writeCliConfig(QTemporaryDir &dir)
{
    QJsonObject auths;
    QJsonObject hub;
    hub.insert(QStringLiteral("auth"), QString::fromLatin1(QByteArrayLiteral("alice:hub-secret").toBase64()));
    auths.insert(QStringLiteral("https://index.docker.io/v1/"), hub);
    QJsonObject ghcr;
    ghcr.insert(QStringLiteral("auth"), QString::fromLatin1(QByteArrayLiteral("bob:ghcr-secret").toBase64()));
    auths.insert(QStringLiteral("ghcr.io"), ghcr);

    QJsonObject root;
    root.insert(QStringLiteral("auths"), auths);
    const QString path = dir.path() + QStringLiteral("/config.json");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return {};
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    file.close();
    return path;
}
} // namespace

void RegistryAuthControllerTest::initTestCase()
{
    setupTranslationDomain();
    qRegisterMetaType<Kontainer::DockerBackendInterface::AuthCheckResult>("Kontainer::DockerBackendInterface::AuthCheckResult");
}

void RegistryAuthControllerTest::init()
{
    m_isolation = new QTemporaryDir();
    QVERIFY(m_isolation->isValid());
    qputenv("DOCKER_CONFIG", m_isolation->path().toUtf8());
}

void RegistryAuthControllerTest::cleanup()
{
    qunsetenv("DOCKER_CONFIG");
    delete m_isolation;
    m_isolation = nullptr;
}

void RegistryAuthControllerTest::loginStoresOnlyAfterSuccessfulCheck()
{
    FakeCredentialBackend wallet;
    CredentialStore store(&wallet);
    store.open();
    MockDockerBackend backend;
    RegistryAuthController controller(&backend, &store);
    controller.refresh();

    // Check fails (401): nothing is written
    backend.setAuthCheckResult(AuthResult::InvalidCredentials, QStringLiteral("unauthorized"));
    controller.login(QStringLiteral("registry.example.com"), QStringLiteral("alice"), QStringLiteral("wrong"));
    QCOMPARE(controller.lastErrorKey(), QStringLiteral("invalidCredentials"));
    QCOMPARE(controller.lastErrorDetail(), QStringLiteral("unauthorized"));
    QVERIFY2(!store.hasCredential(QStringLiteral("registry.example.com")), "a failed check must not be stored");
    QVERIFY(controller.credentials()->empty());

    // Check succeeds: stored in the wallet, the registry appears in the model
    backend.setAuthCheckResult(AuthResult::Succeeded);
    controller.login(QStringLiteral("https://Registry.Example.com/"), QStringLiteral("alice"), QStringLiteral("s3cret"));
    QVERIFY(controller.lastResultKey() == QStringLiteral("loginSucceeded"));
    QVERIFY(controller.lastErrorKey().isEmpty());
    QVERIFY(store.hasCredential(QStringLiteral("registry.example.com")));
    QCOMPARE(controller.credentials()->count(), 1);
    QCOMPARE(controller.credentials()->index(0, 0).data(RegistryCredentialModel::ServerAddressRole).toString(),
             QStringLiteral("registry.example.com"));
    QCOMPARE(controller.credentials()->index(0, 0).data(RegistryCredentialModel::UsernameRole).toString(), QStringLiteral("alice"));
    // The backend gets exactly the entered credential (checked against it, not another registry's)
    QCOMPARE(backend.lastAuthCredential().username, QStringLiteral("alice"));
    QCOMPARE(backend.lastAuthCredential().password, QStringLiteral("s3cret"));
    QCOMPARE(backend.lastAuthServerAddress(), QStringLiteral("registry.example.com"));
}

void RegistryAuthControllerTest::loginRejectsIncompleteInput()
{
    FakeCredentialBackend wallet;
    CredentialStore store(&wallet);
    store.open();
    MockDockerBackend backend;
    RegistryAuthController controller(&backend, &store);

    controller.login(QString(), QStringLiteral("alice"), QStringLiteral("pw"));
    QCOMPARE(controller.lastErrorKey(), QStringLiteral("invalidServerAddress"));

    controller.login(QStringLiteral("registry.example.com"), QStringLiteral("alice"), QString());
    QCOMPARE(controller.lastErrorKey(), QStringLiteral("noCredentials"));

    // Incomplete input sends no request at all
    QCOMPARE(backend.authCheckCount(), 0);
    QVERIFY(controller.credentials()->empty());
}

void RegistryAuthControllerTest::loginRequiresAnAvailableWallet()
{
    FakeCredentialBackend wallet;
    wallet.enabled = false; // the user disabled the wallet
    CredentialStore store(&wallet);
    MockDockerBackend backend;
    RegistryAuthController controller(&backend, &store);
    controller.refresh();

    QCOMPARE(controller.walletStateKey(), QStringLiteral("unavailable"));
    QCOMPARE(controller.walletUnavailableReason(), QStringLiteral("walletDisabled"));

    controller.login(QStringLiteral("registry.example.com"), QStringLiteral("alice"), QStringLiteral("pw"));
    QCOMPARE(controller.lastErrorKey(), QStringLiteral("unavailable"));
    QVERIFY2(backend.authCheckCount() == 0, "no point validating what we cannot store");
    QVERIFY(controller.credentials()->empty());
}

void RegistryAuthControllerTest::tokenLoginIsStoredAsToken()
{
    FakeCredentialBackend wallet;
    CredentialStore store(&wallet);
    store.open();
    MockDockerBackend backend;
    RegistryAuthController controller(&backend, &store);

    controller.login(QStringLiteral("ghcr.io"), QString(), QString(), QStringLiteral("ci-token"));
    QCOMPARE(controller.lastResultKey(), QStringLiteral("loginSucceeded"));
    QVERIFY(store.credential(QStringLiteral("ghcr.io")).usesIdentityToken());
    QCOMPARE(controller.credentials()->index(0, 0).data(RegistryCredentialModel::AuthKindRole).toString(), QStringLiteral("token"));
    QVERIFY2(controller.credentials()->index(0, 0).data(RegistryCredentialModel::UsernameRole).toString().isEmpty(),
             "token logins have no username");
}

void RegistryAuthControllerTest::testCredentialDoesNotModifyAnything()
{
    FakeCredentialBackend wallet;
    CredentialStore store(&wallet);
    store.open();
    MockDockerBackend backend;
    RegistryAuthController controller(&backend, &store);

    controller.login(QStringLiteral("registry.example.com"), QStringLiteral("alice"), QStringLiteral("s3cret"));
    QVERIFY(store.hasCredential(QStringLiteral("registry.example.com")));
    const QByteArray storedBefore = wallet.entries.value(QStringLiteral("registry.example.com"));

    // Test connection: validate the stored credential without touching storage
    backend.setAuthCheckResult(AuthResult::RegistryUnreachable, QStringLiteral("dial tcp: no such host"));
    controller.testCredential(QStringLiteral("registry.example.com"));
    QCOMPARE(controller.lastErrorKey(), QStringLiteral("registryUnreachable"));
    QCOMPARE(wallet.entries.value(QStringLiteral("registry.example.com")), storedBefore);

    // Never-stored registry: say so plainly instead of validating an empty credential
    const int checksBefore = backend.authCheckCount();
    controller.testCredential(QStringLiteral("never.example.com"));
    QCOMPARE(controller.lastErrorKey(), QStringLiteral("noCredentialStored"));
    QCOMPARE(backend.authCheckCount(), checksBefore);
}

void RegistryAuthControllerTest::removeDropsTheCredential()
{
    FakeCredentialBackend wallet;
    CredentialStore store(&wallet);
    store.open();
    MockDockerBackend backend;
    RegistryAuthController controller(&backend, &store);

    controller.login(QStringLiteral("ghcr.io"), QStringLiteral("bob"), QStringLiteral("pw"));
    QCOMPARE(controller.credentials()->count(), 1);

    QSignalSpy credentialsChanged(&controller, &RegistryAuthController::credentialsChanged);
    controller.removeCredential(QStringLiteral("https://GHCR.io/"));
    QCOMPARE(controller.lastResultKey(), QStringLiteral("removed"));
    QVERIFY(!store.hasCredential(QStringLiteral("ghcr.io")));
    QVERIFY(controller.credentials()->empty());
    QCOMPARE(credentialsChanged.count(), 1);

    // Removing an absent registry still succeeds (idempotent) but never claims "removed"
    controller.removeCredential(QStringLiteral("ghcr.io"));
    QVERIFY(controller.lastResultKey() == QStringLiteral("removed"));
}

void RegistryAuthControllerTest::modelNeverExposesSecrets()
{
    FakeCredentialBackend wallet;
    CredentialStore store(&wallet);
    store.open();
    MockDockerBackend backend;
    RegistryAuthController controller(&backend, &store);
    controller.login(QStringLiteral("registry.example.com"), QStringLiteral("alice"), QStringLiteral("s3cret"));

    // The model has address, auth kind and username only: password/token never appear
    const QHash<int, QByteArray> roles = controller.credentials()->roleNames();
    QVERIFY(!roles.values().contains(QByteArrayLiteral("password")));
    QVERIFY(!roles.values().contains(QByteArrayLiteral("identityToken")));
    const QModelIndex index = controller.credentials()->index(0, 0);
    for (int role = Qt::UserRole; role < Qt::UserRole + 8; ++role) {
        const QString value = controller.credentials()->data(index, role).toString();
        QVERIFY2(!value.contains(QStringLiteral("s3cret")), qPrintable(value));
    }
}

/*!
 * Opening the page **silently** picks up CLI-config entries (the sync mechanism was dropped).
 *
 * Registries the user logged into with `docker login` must already be listed when the page opens —
 * there is no "import/sync" button.
 */
void RegistryAuthControllerTest::cliEntriesAreRecognizedSilently()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeCliConfig(dir);
    QVERIFY(!path.isEmpty());
    qputenv("DOCKER_CONFIG", dir.path().toUtf8());

    FakeCredentialBackend wallet;
    CredentialStore store(&wallet);
    store.open();
    MockDockerBackend backend;
    RegistryAuthController controller(&backend, &store);
    controller.refresh();

    QVERIFY(controller.cliConfigPresent());
    QCOMPARE(controller.lastImportedCount(), 2);
    QVERIFY(store.hasCredential(QStringLiteral("ghcr.io")));
    QVERIFY(store.hasCredential(QStringLiteral("index.docker.io")));

    // Idempotent: opening again imports nothing and does not overwrite wallet values
    controller.refresh();
    QCOMPARE(controller.lastImportedCount(), 0);
    QCOMPARE(controller.lastAlreadyPresentCount(), 2);

    qunsetenv("DOCKER_CONFIG");
}

/*!
 * Login and remove both **write back** the CLI config (user requirement: keep it in sync silently).
 */
void RegistryAuthControllerTest::changesAreWrittenBackToTheCliConfig()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeCliConfig(dir);
    QVERIFY(!path.isEmpty());
    qputenv("DOCKER_CONFIG", dir.path().toUtf8());

    FakeCredentialBackend wallet;
    CredentialStore store(&wallet);
    store.open();
    MockDockerBackend backend;
    RegistryAuthController controller(&backend, &store);
    controller.refresh();

    // Login to a new registry → written into the CLI config (so the docker CLI can use it too)
    // Note: what is written is **plaintext-equivalent** base64 — exactly the docker CLI's own format
    controller.login(QStringLiteral("registry.example.com"), QStringLiteral("carol"), QStringLiteral("s3cret"));
    QCOMPARE(controller.lastResultKey(), QStringLiteral("loginSucceeded"));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    const QJsonObject auths = root.value(QStringLiteral("auths")).toObject();
    QVERIFY2(auths.contains(QStringLiteral("registry.example.com")),
             qPrintable(QStringLiteral("keys: %1 err: %2 %3")
                            .arg(auths.keys().join(QLatin1Char(',')))
                            .arg(controller.lastErrorKey())
                            .arg(controller.lastErrorDetail())));
    const QJsonObject entry = auths.value(QStringLiteral("registry.example.com")).toObject();
    QCOMPARE(entry.value(QStringLiteral("auth")).toString(),
             QString::fromLatin1(QByteArrayLiteral("carol:s3cret").toBase64()));
    // Existing entries must survive, **especially the Hub one** — a comparison that used the
    // "registry from an image reference" helper once wrote a new registry's credential into it.
    QCOMPARE(auths.size(), 3);
    QCOMPARE(auths.value(QStringLiteral("https://index.docker.io/v1/")).toObject().value(QStringLiteral("auth")).toString(),
             QString::fromLatin1(QByteArrayLiteral("alice:hub-secret").toBase64()));

    // File permissions: plaintext-equivalent credentials, so 0600 (directory 0700)
    const QFile::Permissions permissions = QFile::permissions(path);
    QVERIFY2(permissions.testFlag(QFile::ReadOwner) && permissions.testFlag(QFile::WriteOwner),
             "the config must stay readable/writable by the owner");
    QVERIFY2(!permissions.testFlag(QFile::ReadGroup) && !permissions.testFlag(QFile::ReadOther),
             "credentials must not be readable by group or others");

    // Remove → also deleted from the CLI config
    controller.removeCredential(QStringLiteral("registry.example.com"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonObject afterRemove = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    QVERIFY(!afterRemove.value(QStringLiteral("auths")).toObject().contains(QStringLiteral("registry.example.com")));
    QCOMPARE(afterRemove.value(QStringLiteral("auths")).toObject().size(), 2);

    qunsetenv("DOCKER_CONFIG");
}

void RegistryAuthControllerTest::walletLifecycleIsReportedToTheUi()
{
    FakeCredentialBackend wallet;
    wallet.synchronousOpen = false; // emulate KWallet: waits for the user to unlock
    CredentialStore store(&wallet);
    MockDockerBackend backend;
    RegistryAuthController controller(&backend, &store);

    QCOMPARE(controller.walletStateKey(), QStringLiteral("closed"));
    controller.refresh();
    QCOMPARE(controller.walletStateKey(), QStringLiteral("opening"));
    wallet.completeOpen();
    QCOMPARE(controller.walletStateKey(), QStringLiteral("ready"));
    QVERIFY(controller.walletUnavailableReason().isEmpty());
}

QTEST_MAIN(RegistryAuthControllerTest)

#include "tst_registry_auth_controller.moc"
