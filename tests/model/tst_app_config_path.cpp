/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/app_config_path.h"

#include <QtTest>

#include <QDir>
#include <QTemporaryDir>

using namespace Kontainer;

/*!
 * 配置文件改名与一次性迁移（软件从 kontainer 更名为 kcm-docker）。
 *
 * 用户已有的挂载预设 / 命令历史存在 `~/.config/kontainerrc`，新版本读 `kcm_dockerrc`——
 * 不迁移就等于把用户数据"弄丢"了（用户看不见，但确实是丢）。
 */
class AppConfigPathTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void migratesTheLegacyFileOnce();
    void keepsAnExistingNewFileUntouched();
    void withoutALegacyFileItJustReturnsTheNewPath();
};

void AppConfigPathTest::migratesTheLegacyFileOnce()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString legacy = dir.filePath(QStringLiteral("kontainerrc"));
    {
        QFile file(legacy);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[MountPresets]\n1\\source=/srv/data\n");
    }

    const QString path = defaultAppConfigPath(dir.path());
    QCOMPARE(path, dir.filePath(QStringLiteral("kcm_dockerrc")));
    QVERIFY2(QFile::exists(path), "the legacy file must be copied to the new name");

    QFile copied(path);
    QVERIFY(copied.open(QIODevice::ReadOnly));
    QVERIFY(copied.readAll().contains("/srv/data"));
    // 旧文件保留：用户回退旧版本还能用
    QVERIFY2(QFile::exists(legacy), "the legacy file must be kept");
}

void AppConfigPathTest::keepsAnExistingNewFileUntouched()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    {
        QFile legacy(dir.filePath(QStringLiteral("kontainerrc")));
        QVERIFY(legacy.open(QIODevice::WriteOnly));
        legacy.write("[MountPresets]\nold\n");
        QFile current(dir.filePath(QStringLiteral("kcm_dockerrc")));
        QVERIFY(current.open(QIODevice::WriteOnly));
        current.write("[MountPresets]\nnew\n");
    }

    QCOMPARE(defaultAppConfigPath(dir.path()), dir.filePath(QStringLiteral("kcm_dockerrc")));
    QFile current(dir.filePath(QStringLiteral("kcm_dockerrc")));
    QVERIFY(current.open(QIODevice::ReadOnly));
    // 迁移只做一次：新文件已经有内容时**不能**被旧文件覆盖
    QVERIFY(current.readAll().contains("new"));
}

void AppConfigPathTest::withoutALegacyFileItJustReturnsTheNewPath()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = defaultAppConfigPath(dir.path());
    QCOMPARE(path, dir.filePath(QStringLiteral("kcm_dockerrc")));
    QVERIFY2(!QFile::exists(path), "nothing must be created when there is nothing to migrate");
}

QTEST_MAIN(AppConfigPathTest)

#include "tst_app_config_path.moc"
