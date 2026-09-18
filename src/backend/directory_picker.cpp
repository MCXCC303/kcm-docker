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

QString SystemDirectoryPicker::chooseDirectory(const QString &startPath)
{
    QString start = startPath;
    // 起点必须是已存在的目录，否则某些平台的原生对话框会拒绝打开
    if (start.isEmpty() || !QFileInfo(start).isDir()) {
        start = QDir::homePath();
    }
    const QString chosen = QFileDialog::getExistingDirectory(nullptr, tr("Choose a directory"), start);
    return chosen;
}

} // namespace Kontainer
