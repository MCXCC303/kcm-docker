/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/docker_backend_interface.h"
#include "support/fake_credential_backend.h"
#include "support/fake_host_path_service.h"
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

    /*!
     * 内存凭据后端（测试与离屏渲染都用它）。
     *
     * 绝不用真实 KWallet：那会弹解锁框、并在开发者/用户的钱包里留下条目。
     * 需要预置凭据的用例直接往 `entries` 里塞，或走控制器的登录流程。
     */
    FakeCredentialBackend *credentialBackend() const
    {
        return m_credentialBackend;
    }

    /*! 测试可以注入探测结果与打开失败（挂载分区的两种状态）。 */
    FakeHostPathService *hostPaths() const
    {
        return m_hostPaths;
    }
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
    FakeHostPathService *m_hostPaths = nullptr;
    FakeCredentialBackend *m_credentialBackend = nullptr;
    StatusController *m_controller = nullptr;
};

} // namespace Kontainer
