/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QObject>
#include <QString>

namespace Kontainer
{

/*!
 * 让用户挑一个目录（挂载预设的宿主路径、构建上下文目录都用它）。
 *
 * 做成接口而不是直接调 `QFileDialog`：
 * - 测试与离屏渲染不能弹出真实对话框（会阻塞），需要注入替身；
 * - KCM 是纯 QML 的形态，弹窗这件事只在这一层发生，QML 侧只拿返回值。
 *
 * 实现走 `QFileDialog::getExistingDirectory`，也就是平台的**原生**文件对话框
 * （在 Plasma 上由 xdg-desktop-portal 提供，即用户熟悉的那个文件管理器界面）。
 */
class DirectoryPicker : public QObject
{
    Q_OBJECT

public:
    explicit DirectoryPicker(QObject *parent = nullptr);
    ~DirectoryPicker() override;

    /*!
     * 返回选中的目录；用户取消或失败时返回空串（调用方保持原值）。
     *
     * `Q_INVOKABLE` 是必需的：QML 只能调用 Q_INVOKABLE / 槽 / 属性，
     * 普通虚函数在 QML 里是 undefined（调用会抛 "is not a function"）。
     */
    Q_INVOKABLE virtual QString chooseDirectory(const QString &startPath) = 0;
};

/*! 真实实现：系统原生目录对话框。 */
class SystemDirectoryPicker : public DirectoryPicker
{
    Q_OBJECT

public:
    explicit SystemDirectoryPicker(QObject *parent = nullptr);

    QString chooseDirectory(const QString &startPath) override;
};

} // namespace Kontainer
