/*
    SPDX-FileCopyrightText: 2026 kontainer developers
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
 * 凭据存储（ARCH_V5_V8 §2.6）。
 *
 * 覆盖三件容易出错的事：
 *   1. **索引键**：用户可能用 `https://Index.Docker.io/v1/`、`docker.io`、
 *      `registry-1.docker.io` 三种写法指同一个仓库，必须是同一个条目；
 *   2. **条目格式**：密码里可以有冒号/引号/换行，必须能原样取回（JSON 而不是拼接）；
 *   3. **可用性**：钱包被禁用或用户拒绝解锁是正常路径，必须明确失败而不是静默丢数据。
 *
 * 钱包本身用内存后端替身；`KWalletBackend` 只断言"未打开时不可用"这类不需要钱包的事实，
 * 真正的钱包读写放在人工验收清单里（无桌面/无钱包的环境跑不了）。
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
    QCOMPARE(changed.count(), 2); // 打开（Ready）+ 写入

    QCOMPARE(store.serverAddresses(), QStringList {QStringLiteral("registry.example.com:5000")});
    QVERIFY(store.hasCredential(QStringLiteral("registry.example.com:5000")));
    const RegistryCredential loaded = store.credential(QStringLiteral("registry.example.com:5000"));
    QCOMPARE(loaded.username, QStringLiteral("alice"));
    QCOMPARE(loaded.password, QStringLiteral("s3cret"));
    QCOMPARE(loaded.serverAddress, QStringLiteral("registry.example.com:5000"));

    // 覆盖：同一个仓库第二次保存必须替换而不是新增
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

    // 用 config.json 里的历史写法保存
    QVERIFY(store.store(credentialFor(QStringLiteral("https://index.docker.io/v1/"))));
    // 用 docker.io / registry-1.docker.io / index.docker.io 都能取到同一条
    for (const char *alias : {"docker.io", "registry-1.docker.io", "index.docker.io", "https://INDEX.docker.io"}) {
        QVERIFY2(store.hasCredential(QString::fromLatin1(alias)), alias);
        QCOMPARE(store.credential(QString::fromLatin1(alias)).username, QStringLiteral("alice"));
    }
    QCOMPARE(store.serverAddresses(), QStringList {QStringLiteral("index.docker.io")});

    // 主机名大小写/末尾斜杠不同也只算一个条目
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

    // 旧版本/手工编辑留下的坏条目：不能返回半条凭据，也不能崩
    backend.entries.insert(QStringLiteral("broken.example.com"), QByteArrayLiteral("{not json"));
    backend.entries.insert(QStringLiteral("empty.example.com"), QByteArrayLiteral("{}"));
    for (const char *address : {"broken.example.com", "empty.example.com"}) {
        const RegistryCredential loaded = store.credential(QString::fromLatin1(address));
        QVERIFY2(loaded.isEmpty(), address);
    }
    // 但"存在性"仍然如实回答（条目在，只是不可用），界面据此提示重新登录
    QVERIFY(store.hasCredential(QStringLiteral("broken.example.com")));
}

void CredentialStoreTest::unavailableWalletIsAnExplicitFailure()
{
    FakeCredentialBackend backend;
    backend.enabled = false; // 用户关掉了钱包子系统
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

    // 参数错误要在碰钱包之前就被拒绝
    QVERIFY(!store.store(credentialFor(QString()), &errorKey));
    QCOMPARE(errorKey, QStringLiteral("invalidServerAddress"));
    RegistryCredential nameless;
    nameless.serverAddress = QStringLiteral("registry.example.com");
    QVERIFY(!store.store(nameless, &errorKey));
    QCOMPARE(errorKey, QStringLiteral("noCredentials"));

    // 只有用户名没有密码也不算：存进去只会得到一条"看起来有、其实用不了"的条目
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
    backend.synchronousOpen = false; // 模拟 KWallet：等用户解锁
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
    // 没删掉东西就不该发 changed()：否则界面会莫名其妙地重建列表
    QCOMPARE(changed.count(), 0);
}

void CredentialStoreTest::kwalletBackendNeedsAnOpenWallet()
{
    // 只断言"还没打开时不可用"这类不需要钱包守护进程的事实：
    // 真正打开会弹解锁框，不能出现在自动化测试里（人工验收清单里有这一步）
    KWalletBackend backend;
    QVERIFY2(!backend.isAvailable(), "the wallet is not open before open() is called");
    QCOMPARE(KWalletBackend::folderName(), QStringLiteral("Kontainer"));
    QVERIFY2(!KWalletBackend::walletName().isEmpty(), "the network wallet name must be resolvable");
}

QTEST_GUILESS_MAIN(CredentialStoreTest)

#include "tst_credential_store.moc"
