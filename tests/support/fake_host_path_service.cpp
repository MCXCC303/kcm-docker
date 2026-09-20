/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "support/fake_host_path_service.h"

namespace Kontainer
{

FakeHostPathService::FakeHostPathService(QObject *parent)
    : HostPathService(parent)
{
}

HostPathState FakeHostPathService::probe(const QString &path) const
{
    if (path.isEmpty()) {
        return HostPathState::NotApplicable;
    }
    return m_state;
}

bool FakeHostPathService::openDirectory(const QString &path)
{
    m_openedPaths.append(path);
    if (m_nextOpenError == HostPathError::None) {
        Q_EMIT openFinished(HostPathError::None, QString());
        return true;
    }
    Q_EMIT openFinished(m_nextOpenError, QStringLiteral("fake failure"));
    return false;
}

} // namespace Kontainer
