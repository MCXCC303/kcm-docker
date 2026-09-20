/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
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
     * `DontUseNativeDialog` is **mandatory**, not a compromise:
     *
     * Plasma's "native" directory dialog is KIO's KFileWidget, which segfaults while painting in
     * kcmshell6 (a QML + QQuickWidget host) — observed stack:
     * QFileDialog::getExistingDirectory → QTreeView::drawRow → libKF6KIOWidgets. Qt's own dialog
     * never enters that code path.
     * Other options: dirs only, no symlink resolution (avoids stalling on slow network paths).
     */
    return int(QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks | QFileDialog::DontUseNativeDialog);
}

QString SystemDirectoryPicker::chooseDirectory(const QString &startPath)
{
    QString start = startPath;
    // Start must be an existing directory or the dialog refuses to open
    if (start.isEmpty() || !QFileInfo(start).isDir()) {
        start = QDir::homePath();
    }
    const auto options = QFileDialog::Options(systemDialogOptions());
    return QFileDialog::getExistingDirectory(nullptr, tr("Choose a directory"), start, options);
}

} // namespace Kontainer
