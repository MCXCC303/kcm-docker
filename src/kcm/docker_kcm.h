/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "model/status_controller.h"

#include <KQuickConfigModule>

namespace Kontainer
{

class DockerBackend;

/*!
 * KCM Layer（ARCH_V1 §6.1）。
 *
 * 只负责：KCM 生命周期、QML 页面加载、把 presentation model 暴露给 QML、
 * 用户触发的刷新。不做 URL 拼接、JSON 解析、HTTP 状态码判断或 socket 访问。
 *
 * 一期是只读状态面板，没有可保存的配置，因此不提供 Apply/Default 按钮（§37）。
 */
class DockerKcm : public KQuickConfigModule
{
    Q_OBJECT

    Q_PROPERTY(Kontainer::StatusController *controller READ controller CONSTANT)

public:
    DockerKcm(QObject *parent, const KPluginMetaData &metaData);
    ~DockerKcm() override;

    StatusController *controller() const
    {
        return m_controller;
    }

private:
    DockerBackend *m_backend = nullptr;
    StatusController *m_controller = nullptr;
};

} // namespace Kontainer
