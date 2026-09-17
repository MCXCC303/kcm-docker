/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/daemon_deployment.h"

#include <QDir>
#include <QFile>
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

    /* 可写性判定：只看这个文件（或它所在目录）能不能写（ARCH_V5_V8 §2.2 修正） */
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

    // 系统级 daemon：SecurityOptions 有内容但不含 rootless
    const EngineInfo info = engineWith({QStringLiteral("name=seccomp,profile=builtin"), QStringLiteral("name=cgroupns")},
                                       dir.path() + QStringLiteral("/.local/share/docker"));

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
    // 引擎信息不完整（例如 /info 失败）：不得猜测形态，也不得猜系统路径、
    // 更不得就此宣布"需要提权"（没有路径就没有写入目标）
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

    // 新装的 rootless daemon 常常还没有 daemon.json：能创建就算可写，
    // 否则会把"用户自己就能写"的文件判成需要 root（界面会平白要一次授权）
    QVERIFY(DaemonDeploymentDetector::configIsWritable(dir.path() + QStringLiteral("/.config/docker/daemon.json")));
}

void DaemonDeploymentTest::missingConfigUnderUnwritableDirIsNotWritable()
{
    // 系统级路径：/etc/docker 或 /etc 不属于当前用户 → 不存在也不能写
    QVERIFY(!DaemonDeploymentDetector::configIsWritable(QStringLiteral("/etc/docker/daemon.json"))
            || QFileInfo(QStringLiteral("/etc")).isWritable());
}

void DaemonDeploymentTest::existingReadOnlyConfigIsNotWritable()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/daemon.json");
    writeFile(path, QByteArrayLiteral("{}"));
    // 父目录可写、文件本身可写
    QVERIFY(DaemonDeploymentDetector::configIsWritable(path));

    // 文件只读（父目录仍然可写）：不能只看父目录，必须看文件自己的权限位
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

    // rootless daemon + 还没有用户配置：不需要提权（可创建），也不该给锁
    const DaemonDeployment deployment =
        DaemonDeploymentDetector::detect(engineWith({QStringLiteral("name=rootless")}), dir.path());
    QCOMPARE(deployment.configPath, dir.path() + QStringLiteral("/.config/docker/daemon.json"));
    QVERIFY(!deployment.configExists);
    QVERIFY2(!deployment.requiresPrivilege(), "a user config the user can create must not require privilege");
}

QTEST_GUILESS_MAIN(DaemonDeploymentTest)

#include "tst_daemon_deployment.moc"
