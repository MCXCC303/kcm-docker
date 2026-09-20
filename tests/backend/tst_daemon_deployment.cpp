/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/daemon_deployment.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace Kontainer;

/*!
 * Daemon deployment form detection (ARCH_V5_V8 §2.2).
 *
 * Detection decides whether to escalate: a wrong verdict either blocks the feature
 * (privilege needed but not requested) or nags with an auth dialog. Pin the matrix here.
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

    /* Writability: only whether this file (or its directory) is writable (ARCH_V5_V8 §2.2 revised) */
    void missingConfigInWritableDirIsWritable();
    void missingConfigUnderUnwritableDirIsNotWritable();
    void existingReadOnlyConfigIsNotWritable();
    void emptyPathIsNeverWritable();
    void missingUserConfigDoesNotRequirePrivilege();
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

    // System daemon: SecurityOptions non-empty but without rootless
    const EngineInfo info = engineWith({QStringLiteral("name=seccomp,profile=builtin"), QStringLiteral("name=cgroupns")},
                                       dir.path() + QStringLiteral("/.local/share/docker"));

    const DaemonDeployment deployment = DaemonDeploymentDetector::detect(info, dir.path());
    QCOMPARE(deployment.formKey(), QStringLiteral("systemRoot"));
    QCOMPARE(deployment.configPath, QStringLiteral("/etc/docker/daemon.json"));
    // On a real machine this file belongs to root, so privilege is needed (what this host does)
    QVERIFY2(deployment.requiresPrivilege() || deployment.configWritable,
             "requiresPrivilege must be false only when the config is actually writable");
    // Data root inside home: warn, users mistake this for rootless
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

    // No SecurityOptions: form unknown, but the user config exists, so use it
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
    // Incomplete engine info (e.g. /info failed): never guess the form or a system path,
    // and never declare "privilege required" (no path means no write target)
    const EngineInfo info; // available=false
    const DaemonDeployment deployment = DaemonDeploymentDetector::detect(info, dir.path());
    QCOMPARE(deployment.formKey(), QStringLiteral("unknown"));
    QVERIFY2(deployment.configPath.isEmpty(), qPrintable(deployment.configPath));
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


void DaemonDeploymentTest::missingConfigInWritableDirIsWritable()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // A fresh rootless daemon often has no daemon.json yet: creatable counts as writable.
    // Otherwise a user-writable file would be judged as needing root (a pointless auth prompt).
    QVERIFY(DaemonDeploymentDetector::configIsWritable(dir.path() + QStringLiteral("/.config/docker/daemon.json")));
}

void DaemonDeploymentTest::missingConfigUnderUnwritableDirIsNotWritable()
{
    // System path: /etc/docker or /etc is not ours, so a missing file is still unwritable
    QVERIFY(!DaemonDeploymentDetector::configIsWritable(QStringLiteral("/etc/docker/daemon.json"))
            || QFileInfo(QStringLiteral("/etc")).isWritable());
}

void DaemonDeploymentTest::existingReadOnlyConfigIsNotWritable()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/daemon.json");
    writeFile(path, QByteArrayLiteral("{}"));
    // Parent directory writable and file itself writable
    QVERIFY(DaemonDeploymentDetector::configIsWritable(path));

    // File read-only while the parent stays writable: check the file's own mode bits
    QVERIFY(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther));
    QVERIFY(!DaemonDeploymentDetector::configIsWritable(path));
}

void DaemonDeploymentTest::emptyPathIsNeverWritable()
{
    QVERIFY(!DaemonDeploymentDetector::configIsWritable(QString()));
}

void DaemonDeploymentTest::missingUserConfigDoesNotRequirePrivilege()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // Rootless daemon without a user config yet: no privilege needed (creatable) and no lock
    const DaemonDeployment deployment =
        DaemonDeploymentDetector::detect(engineWith({QStringLiteral("name=rootless")}), dir.path());
    QCOMPARE(deployment.configPath, dir.path() + QStringLiteral("/.config/docker/daemon.json"));
    QVERIFY(!deployment.configExists);
    QVERIFY2(!deployment.requiresPrivilege(), "a user config the user can create must not require privilege");
}

QTEST_GUILESS_MAIN(DaemonDeploymentTest)

#include "tst_daemon_deployment.moc"
