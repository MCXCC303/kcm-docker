/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/directory_picker.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>

namespace Kontainer
{

DirectoryPicker::DirectoryPicker(QObject *parent)
    : QObject(parent)
{
}

DirectoryPicker::~DirectoryPicker() = default;

SystemDirectoryPicker::SystemDirectoryPicker(QObject *parent)
    : DirectoryPicker(parent)
{
}

int SystemDirectoryPicker::systemDialogOptions()
{
    /*
     * `DontUseNativeDialog` 不是"将就"，而是**必须**：
     *
     * Plasma 下的"原生"目录对话框是 KIO 的 KFileWidget，在 kcmshell6（QML + QQuickWidget 宿主）
     * 里绘制时会段错误（实测栈：QFileDialog::getExistingDirectory → QTreeView::drawRow →
     * libKF6KIOWidgets）。用 Qt 自己的对话框实现就完全不走那条代码路径。
     * 其余选项：只列目录、不解析符号链接（避免在慢速网络路径上卡住）。
     */
    return int(QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks | QFileDialog::DontUseNativeDialog);
}

QString SystemDirectoryPicker::chooseDirectory(const QString &startPath)
{
    QString start = startPath;
    // 起点必须是已存在的目录，否则对话框会拒绝打开
    if (start.isEmpty() || !QFileInfo(start).isDir()) {
        start = QDir::homePath();
    }
    const auto options = QFileDialog::Options(systemDialogOptions());
    return QFileDialog::getExistingDirectory(nullptr, tr("Choose a directory"), start, options);
}

} // namespace Kontainer
