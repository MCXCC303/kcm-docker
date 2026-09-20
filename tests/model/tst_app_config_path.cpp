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
 * Config file rename and one-time migration (the app was renamed kontainer -> kcm-docker).
 *
 * Existing mount presets and command history live in `~/.config/kontainerrc` while new
 * versions read `kcm_dockerrc`, so without migration the user's data is silently lost.
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
    // The legacy file is kept so downgrading still works
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
    // Migration happens once: a non-empty new file must not be overwritten by the old one
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
