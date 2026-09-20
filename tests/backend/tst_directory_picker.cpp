/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/directory_picker.h"

#include <QFileDialog>

#include <QtTest>

using namespace Kontainer;

/*!
 * Directory picking (the "Browse..." of mount presets).
 *
 * Only the dialog configuration is tested here, because the real dialog cannot run in a test:
 * a user saw kcmshell6 **segfault** when clicking "Browse..." in the preset panel,
 * stack `QFileDialog::getExistingDirectory` → KIO's `QTreeView::drawRow` → `libKF6KIOWidgets` SEGV.
 * So Qt's own dialog implementation must be requested explicitly (`DontUseNativeDialog`), and
 * this case pins that down: dropping the option walks back into the crash.
 */
class DirectoryPickerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void avoidsTheInProcessKioDialog();
    void keepsOnlyUsefulOptions();
};

void DirectoryPickerTest::avoidsTheInProcessKioDialog()
{
    const auto options = QFileDialog::Options(SystemDirectoryPicker::systemDialogOptions());
    QVERIFY2(options.testFlag(QFileDialog::DontUseNativeDialog),
             "Plasma's \"native\" dialog is KIO's KFileWidget, which crashed inside kcmshell6");
}

void DirectoryPickerTest::keepsOnlyUsefulOptions()
{
    const auto options = QFileDialog::Options(SystemDirectoryPicker::systemDialogOptions());
    QVERIFY(options.testFlag(QFileDialog::ShowDirsOnly)); // directories only
    QVERIFY(options.testFlag(QFileDialog::DontResolveSymlinks)); // do not stall on slow/network paths
}

QTEST_MAIN(DirectoryPickerTest)

#include "tst_directory_picker.moc"
