/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/daemon_config.h"
#include "backend/privileged_client.h"
#include "support/fake_privileged_client.h"
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

/*! System-wide daemon (this machine): SecurityOptions non-empty but without rootless. */
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

/*!
 * `daemon.json` read/write (ARCH_V5_V8 §2.3/§2.4).
 *
 * The key rule: **unknown keys must be preserved verbatim**. Users may have data-root,
 * features or runtimes we do not understand; a "config editor" losing them is the worst failure.
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
    void serviceControlValidatesAndReports();
    void mergeCanRemoveScalarKeys();
    void periodicRefreshKeepsPendingEdits();
    void removeIntentIsNotAnEmptyEdit();
    void atomicWriteCreatesBackupAndKeepsContent();
    void refusingEmptyContent();
    void backupsAreListedNewestFirst();
    void readBackupRejectsBrokenContent();

    /* Scope and unlock state machine (ARCH_V5_V8 §2.2 fix / UI separation) */
    void scopesPointAtDifferentFiles();
    void protectedScopeIsLockedUntilAuthorized();
    void authorizationExpiresAndLocksAgain();
    void switchingScopeLocksAgain();
    void unlockOnlyAffectsTheRequestingScope();
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
    // data-root is not a managed key: the UI lists it as "other keys", no editing
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

    // Crucial: an unparsable file must never yield new content (one save would destroy the config)
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
    // Our edited key takes effect
    QCOMPARE(root.value(QStringLiteral("registry-mirrors")).toArray().first().toString(), QStringLiteral("https://new.example.com"));
    // Unknown keys preserved one by one (nested objects included)
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
    edits.setRegistryMirrors = true; // empty list = remove the key
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
    edits.concurrentDownloadsEdit = ConfigEdit::Set; // only this one changes
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

    // "Back to default" = remove the key, not write 0 or an empty string
    // (daemon defaults change with the version, and the key may be the user's own)
    const DaemonConfigDocument document = DaemonConfigDocument::fromFile(path);
    DaemonConfigEdits edits;
    edits.concurrentDownloadsEdit = ConfigEdit::Remove;
    edits.logDriverEdit = ConfigEdit::Remove;
    const QJsonObject root = QJsonDocument::fromJson(document.merged(edits)).object();

    QVERIFY2(!root.contains(QStringLiteral("max-concurrent-downloads")), "the key must be gone");
    QVERIFY2(!root.contains(QStringLiteral("log-driver")), "the key must be gone");
    // Keys we do not understand stay untouched
    QCOMPARE(root.value(QStringLiteral("data-root")).toString(), QStringLiteral("/srv"));
}

void DaemonConfigTest::periodicRefreshKeepsPendingEdits()
{
    // Reported bug: the status refresh (setEngineInfo runs on every engine update) reverted
    // in-progress edits to disk values. Auto-refresh may only update "engine facts".
    DaemonConfigController controller;
    controller.setEngineInfo(systemEngine());
    controller.setScope(QStringLiteral("user")); // user scope: independent of this machine's /etc

    controller.setRegistryMirrors({QStringLiteral("https://mirror.example.com")});
    controller.setMaxConcurrentDownloads(9);
    controller.setLogDriver(QStringLiteral("local"));
    QVERIFY(controller.dirty());

    for (int tick = 0; tick < 3; ++tick) {
        controller.setEngineInfo(systemEngine()); // this is the auto-refresh path
        QVERIFY2(controller.dirty(), "periodic refresh must not clear the pending edits");
        QCOMPARE(controller.registryMirrors(), QStringList {QStringLiteral("https://mirror.example.com")});
        QCOMPARE(controller.maxConcurrentDownloads(), 9);
        QCOMPARE(controller.logDriver(), QStringLiteral("local"));
        QVERIFY(controller.pendingContentPreview().contains(QStringLiteral("mirror.example.com")));
    }

    // Only an explicit reload re-reads from disk and drops the edits
    controller.reload();
    QVERIFY(!controller.dirty());
    QVERIFY2(!controller.pendingContentPreview().contains(QStringLiteral("mirror.example.com")),
             "an explicit reload must drop the edits");
    QVERIFY(controller.registryMirrors().isEmpty());
}

void DaemonConfigTest::removeIntentIsNotAnEmptyEdit()
{
    // Removal is an edit too: "no value" must not mean "nothing changed" and block saving
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

    // Empty content writes an empty file (daemon will not start) → must be refused
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
    QTest::qWait(1100); // backup names are second-granular
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
    // A broken backup must never be restored (one click would replace a working config)
    QVERIFY(DaemonConfigWriter::readBackup(broken).isEmpty());

    const QString good = dir.path() + QStringLiteral("/daemon.json.kontainer-backup-20260101-000001");
    writeFile(good, QByteArrayLiteral("{\"data-root\":\"/srv\"}"));
    QVERIFY(!DaemonConfigWriter::readBackup(good).isEmpty());
}

/* --- Scope and unlock state machine --- */


void DaemonConfigTest::scopesPointAtDifferentFiles()
{
    DaemonConfigController controller;
    controller.setEngineInfo(systemEngine());

    controller.setScope(QStringLiteral("system"));
    QCOMPARE(controller.configPath(), QStringLiteral("/etc/docker/daemon.json"));
    // System-wide daemon is running → system scope is the active one
    QVERIFY(controller.activeScope());

    controller.setScope(QStringLiteral("user"));
    QVERIFY2(controller.configPath().endsWith(QStringLiteral("/.config/docker/daemon.json")), qPrintable(controller.configPath()));
    // Only rootless reads user config: inactive under a system daemon (UI shows a banner)
    QVERIFY(!controller.activeScope());
}

void DaemonConfigTest::protectedScopeIsLockedUntilAuthorized()
{
    DaemonConfigController controller;
    controller.setEngineInfo(systemEngine());
    controller.setScope(QStringLiteral("system"));

    // This machine's /etc/docker/daemon.json is root-owned → protected, locked by default
    QVERIFY(controller.requiresPrivilege());
    QVERIFY2(!controller.unlocked(), "a protected scope starts locked");
    QCOMPARE(controller.unlockSecondsRemaining(), 0);

    // Saving while locked is refused (backstop: the UI also disables the button)
    controller.setRegistryMirrors({QStringLiteral("https://mirror.example.com")});
    QVERIFY(!controller.save());
    QCOMPARE(controller.lastError(), QStringLiteral("locked"));
}

void DaemonConfigTest::authorizationExpiresAndLocksAgain()
{
    DaemonConfigController controller;
    controller.setEngineInfo(systemEngine());
    controller.setScope(QStringLiteral("system"));

    FakePrivilegedClient client;
    controller.setPrivilegedClient(&client);

    // Drive unlock: UI clicks Unlock → client requests → authorized (no real polkit here).
    // requestUnlock() first: shared finished() is a broadcast, controller takes only its own.
    controller.requestUnlock();
    QCOMPARE(client.authorizeRequests, 1);
    Q_EMIT client.finished(PrivilegedClient::Operation::Authorize, true, QString());
    QVERIFY(controller.unlocked());
    QVERIFY2(controller.unlockSecondsRemaining() > 0, "unlocking must start the countdown");
    QCOMPARE(controller.lastError(), QString());

    // Explicit lock: the UI's "Lock again" button
    controller.lock();
    QVERIFY(!controller.unlocked());
    QCOMPARE(controller.unlockSecondsRemaining(), 0);

    // Cancelling is a normal outcome: stay locked, but no error banner
    controller.requestUnlock();
    Q_EMIT client.finished(PrivilegedClient::Operation::Authorize, false, QStringLiteral("cancelled"));
    QVERIFY(!controller.unlocked());
    QVERIFY2(controller.lastError().isEmpty(), "cancelling must not raise an error banner");
}

void DaemonConfigTest::switchingScopeLocksAgain()
{
    DaemonConfigController controller;
    controller.setEngineInfo(systemEngine());
    controller.setScope(QStringLiteral("system"));

    FakePrivilegedClient client;
    controller.setPrivilegedClient(&client);
    controller.requestUnlock();
    Q_EMIT client.finished(PrivilegedClient::Operation::Authorize, true, QString());
    QVERIFY(controller.unlocked());

    // Authorization is per file: switching scope must require re-authorization
    controller.setScope(QStringLiteral("user"));
    QVERIFY2(!controller.unlocked(), "switching scope must drop the authorization");
}


/*!
 * B1: service control boundaries and results.
 *
 * Key point: **an invalid request triggers no privileged action** (the only new privilege surface
 * here, so it must hold); valid requests come back on the same channel with a stable key on failure.
 */
void DaemonConfigTest::serviceControlValidatesAndReports()
{
    FakePrivilegedClient client;
    DaemonConfigController controller;
    controller.setPrivilegedClient(&client);
    controller.setEngineInfo(systemEngine());
    QSignalSpy controlledSpy(&controller, &DaemonConfigController::serviceControlled);

    // Unit outside the whitelist: refused, and **no** request was sent
    QVERIFY(!controller.controlService(QStringLiteral("sshd.service"), QStringLiteral("start")));
    QCOMPARE(controller.serviceErrorKey(), QStringLiteral("unitNotManaged"));
    QCOMPARE(client.serviceRequests, 0);

    // Unknown verb: refused the same way
    QVERIFY(!controller.controlService(QStringLiteral("docker.service"), QStringLiteral("mask")));
    QCOMPARE(controller.serviceErrorKey(), QStringLiteral("verbNotManaged"));
    QCOMPARE(client.serviceRequests, 0);

    // Valid request: handed to the client, goes in-flight
    QVERIFY(controller.controlService(QStringLiteral("docker.service"), QStringLiteral("restart")));
    QCOMPARE(client.serviceRequests, 1);
    QCOMPARE(client.lastServiceUnit, QStringLiteral("docker.service"));
    QCOMPARE(client.lastServiceVerb, QStringLiteral("restart"));
    QVERIFY(controller.serviceInFlight());
    QVERIFY(controller.serviceErrorKey().isEmpty());

    // Success: result carries unit/verb, error key empty
    Q_EMIT client.finished(PrivilegedClient::Operation::ServiceControl, true, QString());
    QCOMPARE(controlledSpy.count(), 1);
    QCOMPARE(controlledSpy.at(0).at(0).toString(), QStringLiteral("docker.service"));
    QCOMPARE(controlledSpy.at(0).at(1).toString(), QStringLiteral("restart"));
    QCOMPARE(controlledSpy.at(0).at(2).toBool(), true);
    QVERIFY(!controller.serviceInFlight());

    // Failure (user cancelled): reported honestly, retryable
    QVERIFY(controller.controlService(QStringLiteral("containerd.service"), QStringLiteral("stop")));
    Q_EMIT client.finished(PrivilegedClient::Operation::ServiceControl, false, QStringLiteral("cancelled"));
    QCOMPARE(controlledSpy.count(), 2);
    QCOMPARE(controlledSpy.at(1).at(3).toString(), QStringLiteral("cancelled"));
    QCOMPARE(controller.serviceErrorKey(), QStringLiteral("cancelled"));
    QVERIFY(!controller.serviceInFlight());
}

QTEST_MAIN(DaemonConfigTest)

#include "tst_daemon_config.moc"

void DaemonConfigTest::unlockOnlyAffectsTheRequestingScope()
{
    // DockerKcm creates one PrivilegedConfigClient shared by both scopes, its finished() is a
    // broadcast. Reported bug: system page unlock → lock, then the user page still showed unlocked.
    FakePrivilegedClient client;
    DaemonConfigController user;
    DaemonConfigController system;
    user.setEngineInfo(systemEngine());
    system.setEngineInfo(systemEngine());
    user.setScope(QStringLiteral("user"));
    system.setScope(QStringLiteral("system"));
    user.setPrivilegedClient(&client);
    system.setPrivilegedClient(&client);

    // The system page starts an unlock
    system.requestUnlock();
    QCOMPARE(client.authorizeRequests, 1);
    Q_EMIT client.finished(PrivilegedClient::Operation::Authorize, true, QString());
    QVERIFY2(system.unlocked(), "the requesting scope must be unlocked");
    QVERIFY2(!user.unlocked(), "an unlock started elsewhere must not unlock this scope");

    // The system page locks again; a late authorize result must not re-unlock it
    system.lock();
    Q_EMIT client.finished(PrivilegedClient::Operation::Authorize, true, QString());
    QVERIFY2(!system.unlocked(), "a late result must not re-unlock after an explicit lock");

    // Same for writes: only the initiator gets saved(), the bystander must not see "config written"
    DaemonConfigController writer;
    DaemonConfigController bystander;
    writer.setEngineInfo(systemEngine());
    bystander.setEngineInfo(systemEngine());
    writer.setScope(QStringLiteral("system"));
    bystander.setScope(QStringLiteral("system"));
    writer.setPrivilegedClient(&client);
    bystander.setPrivilegedClient(&client);

    writer.requestUnlock();
    Q_EMIT client.finished(PrivilegedClient::Operation::Authorize, true, QString());
    QVERIFY(writer.unlocked());
    QVERIFY2(!bystander.unlocked(), "the bystander must stay locked");

    writer.setRegistryMirrors({QStringLiteral("https://mirror.example.com")});
    QSignalSpy writerSaved(&writer, &DaemonConfigController::saved);
    QSignalSpy bystanderSaved(&bystander, &DaemonConfigController::saved);
    QVERIFY2(!writer.save(), "the privileged write is asynchronous");
    QCOMPARE(client.writeRequests, 1);
    QCOMPARE(writerSaved.count(), 0);
    Q_EMIT client.finished(PrivilegedClient::Operation::WriteConfig, true, QString());
    QCOMPARE(writerSaved.count(), 1);
    QCOMPARE(bystanderSaved.count(), 0);
    QVERIFY2(bystander.lastError().isEmpty(), "the bystander must not show an error banner for someone else's result");
}

void DaemonConfigTest::privilegeDependsOnWritabilityOnly()
{
    DaemonConfigController controller;

    // Whether privilege is needed depends only on file writability, not the deployment shape:
    // the shape only decides whether a change takes effect. Mixing the two yields bugs
    // like "file is unwritable but no unlock entry point" (saving always fails).
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

    // A rootless daemon is the user's own service: no sudo systemctl in the fallback command
    // (that would touch the system service, unrelated to this scope, and ask for root for nothing)
    controller.setEngineInfo(rootlessEngine());
    controller.setScope(QStringLiteral("user"));
    const QString rootless = controller.privilegedCommand();
    QVERIFY2(rootless.contains(QStringLiteral("systemctl --user restart docker")), qPrintable(rootless));
    QVERIFY2(!rootless.contains(QStringLiteral("sudo systemctl")), qPrintable(rootless));
    // Writes the user config, not /etc
    QVERIFY2(rootless.contains(QStringLiteral("/.config/docker/daemon.json")), qPrintable(rootless));

    controller.setEngineInfo(systemEngine());
    controller.setScope(QStringLiteral("system"));
    const QString system = controller.privilegedCommand();
    QVERIFY2(system.contains(QStringLiteral("sudo tee /etc/docker/daemon.json")), qPrintable(system));
    QVERIFY2(system.contains(QStringLiteral("sudo systemctl restart docker")), qPrintable(system));
}
