/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/host_path_service.h"

#include <QString>
#include <QStringList>

namespace Kontainer
{

/*!
 * Host path service for tests (ARCH_V4 §2.1.1).
 *
 * The production implementation really launches a file manager, which tests must not do: this
 * records which paths were asked to open and allows injecting probe results (exists / missing /
 * not a directory).
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
    /*! Failure for the next openDirectory; None means success. */
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
