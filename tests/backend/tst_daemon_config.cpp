/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/daemon_config.h"
#include "backend/daemon_deployment.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

using namespace Kontainer;

/*!
 * daemon 部署形态探测（ARCH_V5_V8 §2.2）。
 *
 * 探测决定"要不要提权"：判错会造成两种坏结果——该提权时不提权（功能不可用），
 * 或不该提权时弹授权框（打扰用户）。所以这里把矩阵钉死。
 */
class DaemonDeploymentTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void systemDaemonNeedsPrivilegeWhenConfigIsNotWritable();
    void rootlessDaemonUsesUserConfigWithoutPrivilege();
    void userConfigWinsWhenFormIsUnknown();
    void missingSecurityOptionsMeansUnknownForm();
    void dataRootInHomeIsFlagged();
    void configStatsAreReported();
};

namespace
{

EngineInfo engineWith(const QStringList &securityOptions, const QString &dockerRootDir = {})
{
    EngineInfo info;
    info.available = true;
    info.countsAvailable = true;
    info.securityOptions = securityOptions;
    info.dockerRootDir = dockerRootDir;
    return info;
}

void writeFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(content);
    file.close();
}

} // namespace

void DaemonDeploymentTest::systemDaemonNeedsPrivilegeWhenConfigIsNotWritable()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // 系统级 daemon：SecurityOptions 有内容但不含 rootless
    const EngineInfo info = engineWith({QStringLiteral("name=seccomp,profile=builtin"), QStringLiteral("name=cgroupns")},
                                       QStringLiteral("/home/someone/.local/share/docker"));

    const DaemonDeployment deployment = DaemonDeploymentDetector::detect(info, dir.path());
    QCOMPARE(deployment.formKey(), QStringLiteral("systemRoot"));
    QCOMPARE(deployment.configPath, QStringLiteral("/etc/docker/daemon.json"));
    // 真实机器上这个文件不属于当前用户 → 需要提权（本机实测正是这种形态）
    QVERIFY2(deployment.requiresPrivilege() || deployment.configWritable,
             "requiresPrivilege must be false only when the config is actually writable");
    // 数据目录在家目录里：需要提示（用户容易误以为是 rootless）
    QVERIFY(deployment.dataRootInHomeDir);
}

void DaemonDeploymentTest::rootlessDaemonUsesUserConfigWithoutPrivilege()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString userConfig = dir.path() + QStringLiteral("/.config/docker/daemon.json");
    writeFile(userConfig, QByteArrayLiteral("{\"registry-mirrors\":[\"https://mirror.example.com\"]}"));

    const EngineInfo info = engineWith({QStringLiteral("name=seccomp,profile=builtin"), QStringLiteral("name=rootless")});
    const DaemonDeployment deployment = DaemonDeploymentDetector::detect(info, dir.path());

    QCOMPARE(deployment.formKey(), QStringLiteral("rootless"));
    QCOMPARE(deployment.configPath, userConfig);
    QVERIFY(deployment.configExists);
    QVERIFY(deployment.configWritable);
    QVERIFY2(!deployment.requiresPrivilege(), "a rootless config owned by the user must not require privilege");
}

void DaemonDeploymentTest::userConfigWinsWhenFormIsUnknown()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString userConfig = dir.path() + QStringLiteral("/.config/docker/daemon.json");
    writeFile(userConfig, QByteArrayLiteral("{}"));

    // SecurityOptions 为空 → 形态未知，但用户配置存在 → 按用户配置处理
    const DaemonDeployment deployment = DaemonDeploymentDetector::detect(engineWith({}), dir.path());
    QCOMPARE(deployment.formKey(), QStringLiteral("unknown"));
    QCOMPARE(deployment.configPath, userConfig);
    QVERIFY(deployment.configExists);
    QVERIFY2(!deployment.requiresPrivilege(), "an unknown form must never force privilege escalation");
}

void DaemonDeploymentTest::missingSecurityOptionsMeansUnknownForm()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // 引擎信息不完整（例如 /info 失败）：不得猜测形态，也不得提权
    const EngineInfo info; // available=false
    const DaemonDeployment deployment = DaemonDeploymentDetector::detect(info, dir.path());
    QCOMPARE(deployment.formKey(), QStringLiteral("unknown"));
    QVERIFY2(!deployment.requiresPrivilege(), "unknown form must not require privilege");
    QVERIFY(!deployment.configExists);
    QVERIFY(!deployment.configWritable);
}

void DaemonDeploymentTest::dataRootInHomeIsFlagged()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const EngineInfo inside = engineWith({QStringLiteral("name=rootless")}, dir.path() + QStringLiteral("/docker"));
    QVERIFY(DaemonDeploymentDetector::detect(inside, dir.path()).dataRootInHomeDir);

    const EngineInfo outside = engineWith({QStringLiteral("name=rootless")}, QStringLiteral("/var/lib/docker"));
    QVERIFY(!DaemonDeploymentDetector::detect(outside, dir.path()).dataRootInHomeDir);
}

void DaemonDeploymentTest::configStatsAreReported()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString userConfig = dir.path() + QStringLiteral("/.config/docker/daemon.json");
    writeFile(userConfig, QByteArrayLiteral("{\"log-driver\":\"json-file\"}"));

    const EngineInfo info = engineWith({QStringLiteral("name=rootless")});
    const DaemonDeployment deployment = DaemonDeploymentDetector::detect(info, dir.path());
    QVERIFY(deployment.configSize > 0);
    QVERIFY(deployment.configModified.isValid());
}

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
    void atomicWriteCreatesBackupAndKeepsContent();
    void refusingEmptyContent();
    void backupsAreListedNewestFirst();
    void readBackupRejectsBrokenContent();
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
    edits.maxConcurrentDownloads = 3; // 只改这一项
    const QJsonObject root = QJsonDocument::fromJson(document.merged(edits)).object();
    QCOMPARE(root.value(QStringLiteral("registry-mirrors")).toArray().first().toString(), QStringLiteral("https://keep.example.com"));
    QCOMPARE(root.value(QStringLiteral("log-driver")).toString(), QStringLiteral("json-file"));
    QCOMPARE(root.value(QStringLiteral("max-concurrent-downloads")).toInt(), 3);
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

QTEST_MAIN(DaemonConfigTest)

#include "tst_daemon_config.moc"
