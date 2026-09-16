/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/docker_backend_interface.h"
#include "model/status_controller.h"

#include <QObject>
#include <QString>

namespace Kontainer
{

/*!
 * QML 加载测试用的 KCM 替身（ARCH_V2 §46）。
 *
 * 提供 QML 里用到的 `kcm` 上下文属性（controller / name / description），
 * 让界面文件可以在没有 kcmshell6 的情况下被加载并检查错误。
 */
class QmlStubKcm : public QObject
{
    Q_OBJECT

    Q_PROPERTY(Kontainer::StatusController *controller READ controller CONSTANT)
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString description READ description CONSTANT)

public:
    explicit QmlStubKcm(DockerBackendInterface *backend, QObject *parent = nullptr);
    ~QmlStubKcm() override;

    StatusController *controller() const
    {
        return m_controller;
    }
    QString name() const
    {
        return QStringLiteral("Docker");
    }
    QString description() const
    {
        return QStringLiteral("Test stub KCM");
    }

private:
    StatusController *m_controller = nullptr;
};

} // namespace Kontainer
