/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QObject>
#include <QString>

namespace Kontainer
{

/*!
 * Lets the user pick a directory (mount preset host paths, build context dirs).
 *
 * An interface instead of calling `QFileDialog` directly:
 * - tests and offscreen rendering must not open a real dialog (it blocks), so a stub is injected;
 * - the KCM is pure QML, so the dialog stays in this layer and QML only receives the result.
 *
 * The implementation uses `QFileDialog::getExistingDirectory` but **forces Qt's own dialog**
 * (`DontUseNativeDialog`): Plasma's "native" dialog is KIO's KFileWidget, which crashes in QML
 * hosts such as kcmshell6 — observed stack: `QFileDialog::getExistingDirectory` → KIO QTreeView
 * painting → `libKF6KIOWidgets` SEGV. Qt's own implementation avoids that path (see
 * `systemDialogOptions()` and the matching test case).
 */
class DirectoryPicker : public QObject
{
    Q_OBJECT

public:
    explicit DirectoryPicker(QObject *parent = nullptr);
    ~DirectoryPicker() override;

    /*!
     * Returns the chosen directory; empty string on cancel or failure (the caller keeps its value).
     *
     * `Q_INVOKABLE` is required: QML can only call Q_INVOKABLE / slots / properties, so a plain
     * virtual function is undefined there (the call throws "is not a function").
     */
    Q_INVOKABLE virtual QString chooseDirectory(const QString &startPath) = 0;
};

/*! Real implementation: the native system directory dialog. */
class SystemDirectoryPicker : public DirectoryPicker
{
    Q_OBJECT

public:
    explicit SystemDirectoryPicker(QObject *parent = nullptr);

    /*!
     * Options used by the directory dialog.
     *
     * Exposed separately so a **test** can pin "must avoid KIO's in-process dialog": removing
     * `DontUseNativeDialog` walks right back into the crashing path.
     */
    static int systemDialogOptions();

    QString chooseDirectory(const QString &startPath) override;
};

} // namespace Kontainer
