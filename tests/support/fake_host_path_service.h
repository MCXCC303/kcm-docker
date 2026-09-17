/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/host_path_service.h"

#include <QString>
#include <QStringList>

namespace Kontainer
{

/*!
 * 测试用的宿主路径服务（ARCH_V4 §2.1.1）。
 *
 * 生产实现会真的拉起文件管理器，测试显然不能这么干：
 * 这里记录「谁被请求打开」，并允许注入探测结果（路径存在 / 缺失 / 不是目录）。
 */
class FakeHostPathService : public HostPathService
{
    Q_OBJECT

public:
    explicit FakeHostPathService(QObject *parent = nullptr);

    void setState(HostPathState state)
    {
        m_state = state;
    }
    /*! 下一次 openDirectory 的失败结果；None 表示成功。 */
    void setNextOpenError(HostPathError error)
    {
        m_nextOpenError = error;
    }
    QStringList openedPaths() const
    {
        return m_openedPaths;
    }
    int openCount() const
    {
        return int(m_openedPaths.size());
    }
    void clear()
    {
        m_openedPaths.clear();
        m_nextOpenError = HostPathError::None;
    }

    HostPathState probe(const QString &path) const override;
    bool openDirectory(const QString &path) override;

private:
    HostPathState m_state = HostPathState::Directory;
    HostPathError m_nextOpenError = HostPathError::None;
    QStringList m_openedPaths;
};

} // namespace Kontainer
