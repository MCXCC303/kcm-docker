/*
    SPDX-FileCopyrightText: 2026 kontainer developers
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
 * 认证管理控制器（ARCH_V5_V8 §2.6/§2.7）。
 *
 * 这里钉的是**规则**而不是界面：
 *   1. 登录必须先过 `POST /auth`，失败**绝不落盘**；
 *   2. 钱包不可用时只降级（明确报错、不回退明文、不假装成功）；
 *   3. 模型里只有仓库地址与用户名——密码/令牌不进模型；
 *   4. CLI 导入只读、不覆盖，助手指管的条目不猜。
 */
class RegistryAuthControllerTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir *m_isolation = nullptr;

private Q_SLOTS:
    void initTestCase();
    /*!
     * 每个用例前把 `DOCKER_CONFIG` 指到空目录。
     *
     * 这些用例会调 `refresh()`（静默识别 CLI 配置）与登录/移除（**写回** CLI 配置），
     * 不隔离的话就会读改用户真实的 `~/.docker/config.json`——那是他的登录状态。
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

    // 校验失败（401）：什么都不写
    backend.setAuthCheckResult(AuthResult::InvalidCredentials, QStringLiteral("unauthorized"));
    controller.login(QStringLiteral("registry.example.com"), QStringLiteral("alice"), QStringLiteral("wrong"));
    QCOMPARE(controller.lastErrorKey(), QStringLiteral("invalidCredentials"));
    QCOMPARE(controller.lastErrorDetail(), QStringLiteral("unauthorized"));
    QVERIFY2(!store.hasCredential(QStringLiteral("registry.example.com")), "a failed check must not be stored");
    QVERIFY(controller.credentials()->empty());

    // 校验成功：写入钱包，模型出现该仓库
    backend.setAuthCheckResult(AuthResult::Succeeded);
    controller.login(QStringLiteral("https://Registry.Example.com/"), QStringLiteral("alice"), QStringLiteral("s3cret"));
    QVERIFY(controller.lastResultKey() == QStringLiteral("loginSucceeded"));
    QVERIFY(controller.lastErrorKey().isEmpty());
    QVERIFY(store.hasCredential(QStringLiteral("registry.example.com")));
    QCOMPARE(controller.credentials()->count(), 1);
    QCOMPARE(controller.credentials()->index(0, 0).data(RegistryCredentialModel::ServerAddressRole).toString(),
             QStringLiteral("registry.example.com"));
    QCOMPARE(controller.credentials()->index(0, 0).data(RegistryCredentialModel::UsernameRole).toString(), QStringLiteral("alice"));
    // 传给后端的凭据就是用户输入的那条（校验用的是它，而不是别的仓库的）
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

    // 参数不全时一个请求都不发
    QCOMPARE(backend.authCheckCount(), 0);
    QVERIFY(controller.credentials()->empty());
}

void RegistryAuthControllerTest::loginRequiresAnAvailableWallet()
{
    FakeCredentialBackend wallet;
    wallet.enabled = false; // 用户关掉了钱包
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

    // 测试连接：用已保存的凭据校验，不改动存储
    backend.setAuthCheckResult(AuthResult::RegistryUnreachable, QStringLiteral("dial tcp: no such host"));
    controller.testCredential(QStringLiteral("registry.example.com"));
    QCOMPARE(controller.lastErrorKey(), QStringLiteral("registryUnreachable"));
    QCOMPARE(wallet.entries.value(QStringLiteral("registry.example.com")), storedBefore);

    // 没有保存过的仓库：明确说"没存过"，而不是发一个空凭据去校验
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

    // 移除不存在的仓库：同样成功（幂等），但不会谎报"已移除"
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

    // 模型里只有地址、登录方式与用户名：密码/令牌永远不出现
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
 * 打开页面时**静默**识别 CLI 配置里的条目（取消同步机制后的行为）。
 *
 * 用户在 CLI 里 `docker login` 过的仓库，打开这个页面就应该已经在列表里——
 * 没有任何"导入/同步"按钮。
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

    // 幂等：再打开一次不会重复导入，也不会覆盖钱包里的值
    controller.refresh();
    QCOMPARE(controller.lastImportedCount(), 0);
    QCOMPARE(controller.lastAlreadyPresentCount(), 2);

    qunsetenv("DOCKER_CONFIG");
}

/*!
 * 登录 / 移除都会**同步写回** CLI 配置文件（用户要求：静默维护同步）。
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

    // 登录一个新仓库 → 写进 CLI 配置（docker CLI 因此也能用）
    // 注意：登录时写入的是**明文等价**的 base64，这正是 docker CLI 自己的格式
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
    // 文件里原有的条目必须保留，**Hub 那一条尤其不能被改**（曾经因为用了
    // "从镜像引用推仓库"的函数做比较，把新仓库的凭据写进了 Hub 条目里）
    QCOMPARE(auths.size(), 3);
    QCOMPARE(auths.value(QStringLiteral("https://index.docker.io/v1/")).toObject().value(QStringLiteral("auth")).toString(),
             QString::fromLatin1(QByteArrayLiteral("alice:hub-secret").toBase64()));

    // 文件权限：等价于明文凭据，必须是 0600（目录 0700）
    const QFile::Permissions permissions = QFile::permissions(path);
    QVERIFY2(permissions.testFlag(QFile::ReadOwner) && permissions.testFlag(QFile::WriteOwner),
             "the config must stay readable/writable by the owner");
    QVERIFY2(!permissions.testFlag(QFile::ReadGroup) && !permissions.testFlag(QFile::ReadOther),
             "credentials must not be readable by group or others");

    // 移除 → 同步从 CLI 配置里删掉
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
    wallet.synchronousOpen = false; // 模拟 KWallet：等用户解锁
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
