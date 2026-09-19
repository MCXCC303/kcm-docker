/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/directory_picker.h"

#include <QFileDialog>

#include <QtTest>

using namespace Kontainer;

/*!
 * 目录选择（挂载预设的"浏览…"）。
 *
 * 这里只测"对话框的配置"这一条，因为真正弹出对话框没法在测试里跑：
 * 用户实测在预设面板点"浏览…"时 kcmshell6 **段错误**，
 * 栈是 `QFileDialog::getExistingDirectory` → KIO 的 `QTreeView::drawRow` → `libKF6KIOWidgets` SEGV。
 * 结论是必须显式要求 Qt 自己的对话框实现（`DontUseNativeDialog`），
 * 用例把这条钉死：去掉该选项就会重新走回崩溃的路径。
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
    QVERIFY(options.testFlag(QFileDialog::ShowDirsOnly)); // 只挑目录
    QVERIFY(options.testFlag(QFileDialog::DontResolveSymlinks)); // 慢速/网络路径上不要卡住
}

QTEST_MAIN(DirectoryPickerTest)

#include "tst_directory_picker.moc"
