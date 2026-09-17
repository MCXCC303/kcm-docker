/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/daemon_config.h"
#include "backend/privileged_config_client.h"
#include "model/daemon_config_controller.h"
#include "backend/daemon_deployment.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

using namespace Kontainer;

namespace
{
void writeFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(content);
    file.close();
}

} // namespace

/*!
 * `daemon.json` 读写（ARCH_V5_V8 §2.3/§2.4）。
 *
 * 最重要的一条：**未知键必须原样保留**。用户可能有 data-root、features、runtimes
 * 等我们不懂的配置，一个"配置编辑器"把它们弄丢是最不可接受的失败方式。
 */
class DaemonConfigTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void readsManagedKeys();
    void missingFileIsValidAndEmpty();
    void invalidJsonIsReadOnlyAndNeverOverwritten();
    void mergePreservesUnknownKeys();
    void mergeCanRemoveASetting();
    void mergeOnlyTouchesRequestedKeys();
    void mergeCanRemoveScalarKeys();
    void removeIntentIsNotAnEmptyEdit();
    void atomicWriteCreatesBackupAndKeepsContent();
    void refusingEmptyContent();
    void backupsAreListedNewestFirst();
    void readBackupRejectsBrokenContent();

    /* 作用域与解锁状态机（ARCH_V5_V8 §2.2 修正 / 用户界面分离） */
    void scopesPointAtDifferentFiles();
    void protectedScopeIsLockedUntilAuthorized();
    void authorizationExpiresAndLocksAgain();
    void switchingScopeLocksAgain();
    void privilegeDependsOnWritabilityOnly();
    void manualCommandRestartsTheRightService();
};

void DaemonConfigTest::readsManagedKeys()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/daemon.json");
    writeFile(path,
              QByteArrayLiteral("{\n"
                                "  \"registry-mirrors\": [\"https://mirror.example.com\", \"http://one.local\"],\n"
                                "  \"insecure-registries\": [\"registry.local:5000\"],\n"
                                "  \"max-concurrent-downloads\": 5,\n"
                                "  \"log-driver\": \"json-file\",\n"
                                "  \"data-root\": \"/srv/docker\"\n"
                                "}\n"));

    const DaemonConfigDocument document = DaemonConfigDocument::fromFile(path);
    QVERIFY(document.exists());
    QVERIFY(document.isValid());
    QCOMPARE(document.registryMirrors(), QStringList({QStringLiteral("https://mirror.example.com"), QStringLiteral("http://one.local")}));
    QCOMPARE(document.insecureRegistries(), QStringList {QStringLiteral("registry.local:5000")});
    QCOMPARE(document.maxConcurrentDownloads(), 5);
    QCOMPARE(document.logDriver(), QStringLiteral("json-file"));
    QCOMPARE(document.dataRoot(), QStringLiteral("/srv/docker"));
    // data-root 不是我们管理的键：界面只显示"其他键"，不提供编辑
    QCOMPARE(document.unmanagedKeys(), QStringList {QStringLiteral("data-root")});
}

void DaemonConfigTest::missingFileIsValidAndEmpty()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const DaemonConfigDocument document = DaemonConfigDocument::fromFile(dir.path() + QStringLiteral("/nope.json"));
    QVERIFY2(!document.exists(), "a missing config file is not an error");
    QVERIFY(document.isValid());
    QVERIFY(document.registryMirrors().isEmpty());
}

void DaemonConfigTest::invalidJsonIsReadOnlyAndNeverOverwritten()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/daemon.json");
    writeFile(path, QByteArrayLiteral("{ this is not json"));

    const DaemonConfigDocument document = DaemonConfigDocument::fromFile(path);
    QVERIFY(document.exists());
    QVERIFY2(!document.isValid(), "broken JSON must be reported as invalid");
    QVERIFY(!document.errorText().isEmpty());

    // 关键：看不懂的文件不允许产生新内容（否则一次保存就会毁掉用户的配置）
    DaemonConfigEdits edits;
    edits.setRegistryMirrors = true;
    edits.registryMirrors = {QStringLiteral("https://mirror.example.com")};
    QVERIFY2(document.merged(edits).isEmpty(), "refusing to rewrite an unparsable config");
}

void DaemonConfigTest::mergePreservesUnknownKeys()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/daemon.json");
    writeFile(path,
              QByteArrayLiteral("{\n"
                                "  \"data-root\": \"/home/thf/.local/share/docker/\",\n"
                                "  \"features\": {\"buildkit\": true},\n"
                                "  \"runtimes\": {\"custom\": {\"path\": \"/usr/bin/custom\"}},\n"
                                "  \"registry-mirrors\": [\"https://old.example.com\"]\n"
                                "}\n"));

    const DaemonConfigDocument document = DaemonConfigDocument::fromFile(path);
    QVERIFY(document.isValid());

    DaemonConfigEdits edits;
    edits.setRegistryMirrors = true;
    edits.registryMirrors = {QStringLiteral("https://new.example.com")};
    const QByteArray merged = document.merged(edits);
    QVERIFY(!merged.isEmpty());

    const QJsonObject root = QJsonDocument::fromJson(merged).object();
    // 我们改的键生效
    QCOMPARE(root.value(QStringLiteral("registry-mirrors")).toArray().first().toString(), QStringLiteral("https://new.example.com"));
    // 未知键逐键保留（包括嵌套对象）
    QCOMPARE(root.value(QStringLiteral("data-root")).toString(), QStringLiteral("/home/thf/.local/share/docker/"));
    QCOMPARE(root.value(QStringLiteral("features")).toObject().value(QStringLiteral("buildkit")).toBool(), true);
    QCOMPARE(root.value(QStringLiteral("runtimes")).toObject().value(QStringLiteral("custom")).toObject().value(QStringLiteral("path")).toString(),
             QStringLiteral("/usr/bin/custom"));
}

void DaemonConfigTest::mergeCanRemoveASetting()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/daemon.json");
    writeFile(path, QByteArrayLiteral("{\"registry-mirrors\":[\"https://old.example.com\"],\"data-root\":\"/srv\"}"));

    const DaemonConfigDocument document = DaemonConfigDocument::fromFile(path);
    DaemonConfigEdits edits;
    edits.setRegistryMirrors = true; // 空列表 = 删除该键
    const QJsonObject root = QJsonDocument::fromJson(document.merged(edits)).object();
    QVERIFY2(!root.contains(QStringLiteral("registry-mirrors")), "clearing the list must remove the key");
    QCOMPARE(root.value(QStringLiteral("data-root")).toString(), QStringLiteral("/srv"));
}

void DaemonConfigTest::mergeOnlyTouchesRequestedKeys()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/daemon.json");
    writeFile(path, QByteArrayLiteral("{\"registry-mirrors\":[\"https://keep.example.com\"],\"log-driver\":\"json-file\"}"));

    const DaemonConfigDocument document = DaemonConfigDocument::fromFile(path);
    DaemonConfigEdits edits;
    edits.concurrentDownloadsEdit = ConfigEdit::Set; // 只改这一项
    edits.maxConcurrentDownloads = 3;
    const QJsonObject root = QJsonDocument::fromJson(document.merged(edits)).object();
    QCOMPARE(root.value(QStringLiteral("registry-mirrors")).toArray().first().toString(), QStringLiteral("https://keep.example.com"));
    QCOMPARE(root.value(QStringLiteral("log-driver")).toString(), QStringLiteral("json-file"));
    QCOMPARE(root.value(QStringLiteral("max-concurrent-downloads")).toInt(), 3);
}

void DaemonConfigTest::mergeCanRemoveScalarKeys()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/daemon.json");
    writeFile(path,
              QByteArrayLiteral("{\"max-concurrent-downloads\":5,\"log-driver\":\"json-file\",\"data-root\":\"/srv\"}"));

    // 「回到默认」= 删掉这个键：不是写 0，也不写空串
    // （daemon 的默认值会随版本变化，而且那个键可能是用户自己写的）
    const DaemonConfigDocument document = DaemonConfigDocument::fromFile(path);
    DaemonConfigEdits edits;
    edits.concurrentDownloadsEdit = ConfigEdit::Remove;
    edits.logDriverEdit = ConfigEdit::Remove;
    const QJsonObject root = QJsonDocument::fromJson(document.merged(edits)).object();

    QVERIFY2(!root.contains(QStringLiteral("max-concurrent-downloads")), "the key must be gone");
    QVERIFY2(!root.contains(QStringLiteral("log-driver")), "the key must be gone");
    // 我们不懂的键照旧保留
    QCOMPARE(root.value(QStringLiteral("data-root")).toString(), QStringLiteral("/srv"));
}

void DaemonConfigTest::removeIntentIsNotAnEmptyEdit()
{
    // 纯删除也是编辑：不能因为"没有值"就被当成"什么都没改"而拒绝保存
    DaemonConfigEdits edits;
    edits.logDriverEdit = ConfigEdit::Remove;
    QVERIFY(!edits.isEmpty());

    DaemonConfigEdits untouched;
    QVERIFY(untouched.isEmpty());
}

void DaemonConfigTest::atomicWriteCreatesBackupAndKeepsContent()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/daemon.json");
    const QByteArray original = QByteArrayLiteral("{\n  \"data-root\": \"/srv\"\n}\n");
    writeFile(path, original);

    const QByteArray updated = QByteArrayLiteral("{\n  \"data-root\": \"/srv\",\n  \"log-driver\": \"json-file\"\n}\n");
    QString backup;
    QCOMPARE(DaemonConfigWriter::writeAtomically(path, updated, &backup), QString());
    QVERIFY2(!backup.isEmpty(), "every save must leave a backup");

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), updated);
    file.close();

    QFile backupFile(backup);
    QVERIFY(backupFile.open(QIODevice::ReadOnly));
    QCOMPARE(backupFile.readAll(), original);
}

void DaemonConfigTest::refusingEmptyContent()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/daemon.json");
    writeFile(path, QByteArrayLiteral("{\"data-root\":\"/srv\"}"));

    // 空内容会写出一个空文件（daemon 直接起不来）→ 必须拒绝
    QVERIFY(!DaemonConfigWriter::writeAtomically(path, QByteArray(), nullptr).isEmpty());
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(!file.readAll().isEmpty());
}

void DaemonConfigTest::backupsAreListedNewestFirst()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/daemon.json");
    writeFile(path, QByteArrayLiteral("{\"data-root\":\"/srv\"}"));

    QCOMPARE(DaemonConfigWriter::writeAtomically(path, QByteArrayLiteral("{\"data-root\":\"/srv\",\"a\":1}"), nullptr), QString());
    QTest::qWait(1100); // 备份名精确到秒
    QCOMPARE(DaemonConfigWriter::writeAtomically(path, QByteArrayLiteral("{\"data-root\":\"/srv\",\"a\":2}"), nullptr), QString());

    const QStringList backups = DaemonConfigWriter::listBackups(path);
    QCOMPARE(backups.size(), 2);
    QVERIFY2(backups.first() > backups.last(), "newest backup must come first");
}

void DaemonConfigTest::readBackupRejectsBrokenContent()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString broken = dir.path() + QStringLiteral("/daemon.json.kontainer-backup-20260101-000000");
    writeFile(broken, QByteArrayLiteral("{ broken"));
    // 坏备份不能拿来"恢复"（否则一次点击就把可用配置换成起不来的配置）
    QVERIFY(DaemonConfigWriter::readBackup(broken).isEmpty());

    const QString good = dir.path() + QStringLiteral("/daemon.json.kontainer-backup-20260101-000001");
    writeFile(good, QByteArrayLiteral("{\"data-root\":\"/srv\"}"));
    QVERIFY(!DaemonConfigWriter::readBackup(good).isEmpty());
}

/* --- 作用域与解锁状态机 --- */

namespace
{
EngineInfo systemEngine()
{
    EngineInfo info;
    info.available = true;
    info.securityOptions = {QStringLiteral("name=seccomp,profile=builtin"), QStringLiteral("name=cgroupns")};
    return info;
}

EngineInfo rootlessEngine()
{
    EngineInfo info;
    info.available = true;
    info.securityOptions = {QStringLiteral("name=seccomp,profile=builtin"), QStringLiteral("name=rootless")};
    return info;
}
} // namespace

void DaemonConfigTest::scopesPointAtDifferentFiles()
{
    DaemonConfigController controller;
    controller.setEngineInfo(systemEngine());

    controller.setScope(QStringLiteral("system"));
    QCOMPARE(controller.configPath(), QStringLiteral("/etc/docker/daemon.json"));
    // 系统级 daemon 正在运行 → 系统作用域是"生效的那个"
    QVERIFY(controller.activeScope());

    controller.setScope(QStringLiteral("user"));
    QVERIFY2(controller.configPath().endsWith(QStringLiteral("/.config/docker/daemon.json")), qPrintable(controller.configPath()));
    // rootless 才读用户配置：系统级 daemon 下这个作用域不生效（界面据此给横幅）
    QVERIFY(!controller.activeScope());
}

void DaemonConfigTest::protectedScopeIsLockedUntilAuthorized()
{
    DaemonConfigController controller;
    controller.setEngineInfo(systemEngine());
    controller.setScope(QStringLiteral("system"));

    // 本机 /etc/docker/daemon.json 属于 root → 受保护、默认锁定
    QVERIFY(controller.requiresPrivilege());
    QVERIFY2(!controller.unlocked(), "a protected scope starts locked");
    QCOMPARE(controller.unlockSecondsRemaining(), 0);

    // 未解锁时保存被拒绝（兜底：界面也会禁用按钮）
    controller.setRegistryMirrors({QStringLiteral("https://mirror.example.com")});
    QVERIFY(!controller.save());
    QCOMPARE(controller.lastError(), QStringLiteral("locked"));
}

void DaemonConfigTest::authorizationExpiresAndLocksAgain()
{
    DaemonConfigController controller;
    controller.setEngineInfo(systemEngine());
    controller.setScope(QStringLiteral("system"));

    PrivilegedConfigClient client;
    controller.setPrivilegedClient(&client);

    // 驱动解锁：直接发客户端的授权成功信号（测试不真的去走 polkit）
    Q_EMIT client.finished(PrivilegedConfigClient::Operation::Authorize, true, QString());
    QVERIFY(controller.unlocked());
    QVERIFY2(controller.unlockSecondsRemaining() > 0, "unlocking must start the countdown");
    QCOMPARE(controller.lastError(), QString());

    // 主动上锁：界面上的「重新上锁」
    controller.lock();
    QVERIFY(!controller.unlocked());
    QCOMPARE(controller.unlockSecondsRemaining(), 0);

    // 取消授权是正常结果：保持锁定，但不应变成错误横幅
    Q_EMIT client.finished(PrivilegedConfigClient::Operation::Authorize, false, QStringLiteral("cancelled"));
    QVERIFY(!controller.unlocked());
    QVERIFY2(controller.lastError().isEmpty(), "cancelling must not raise an error banner");
}

void DaemonConfigTest::switchingScopeLocksAgain()
{
    DaemonConfigController controller;
    controller.setEngineInfo(systemEngine());
    controller.setScope(QStringLiteral("system"));

    PrivilegedConfigClient client;
    controller.setPrivilegedClient(&client);
    Q_EMIT client.finished(PrivilegedConfigClient::Operation::Authorize, true, QString());
    QVERIFY(controller.unlocked());

    // 授权是给"那个文件"的：换作用域必须重新授权
    controller.setScope(QStringLiteral("user"));
    QVERIFY2(!controller.unlocked(), "switching scope must drop the authorization");
}

QTEST_MAIN(DaemonConfigTest)

#include "tst_daemon_config.moc"

void DaemonConfigTest::privilegeDependsOnWritabilityOnly()
{
    DaemonConfigController controller;

    // "要不要提权"只由这个文件能不能写决定，与部署形态无关：
    // 形态只决定"改了会不会生效"。两者混起来就会出现
    // "文件写不了却不给解锁入口"（保存必然失败）这类错误。
    for (const EngineInfo &engine : {systemEngine(), rootlessEngine()}) {
        controller.setEngineInfo(engine);
        for (const QString &scope : {QStringLiteral("system"), QStringLiteral("user")}) {
            controller.setScope(scope);
            QVERIFY2(controller.requiresPrivilege() == !controller.configWritable(),
                     qPrintable(QStringLiteral("%1/%2: requiresPrivilege must mirror configWritable")
                                    .arg(engine.securityOptions.join(QLatin1Char(',')), scope)));
        }
    }
}

void DaemonConfigTest::manualCommandRestartsTheRightService()
{
    DaemonConfigController controller;

    // rootless daemon 是用户自己的服务：降级命令里不能出现 sudo systemctl
    // （那动的是系统服务，与本作用域无关，而且会平白多要一次 root）
    controller.setEngineInfo(rootlessEngine());
    controller.setScope(QStringLiteral("user"));
    const QString rootless = controller.privilegedCommand();
    QVERIFY2(rootless.contains(QStringLiteral("systemctl --user restart docker")), qPrintable(rootless));
    QVERIFY2(!rootless.contains(QStringLiteral("sudo systemctl")), qPrintable(rootless));
    // 写的是用户配置，不是 /etc
    QVERIFY2(rootless.contains(QStringLiteral("/.config/docker/daemon.json")), qPrintable(rootless));

    controller.setEngineInfo(systemEngine());
    controller.setScope(QStringLiteral("system"));
    const QString system = controller.privilegedCommand();
    QVERIFY2(system.contains(QStringLiteral("sudo tee /etc/docker/daemon.json")), qPrintable(system));
    QVERIFY2(system.contains(QStringLiteral("sudo systemctl restart docker")), qPrintable(system));
}
