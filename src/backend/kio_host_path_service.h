/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/host_path_service.h"

namespace Kontainer
{

class KioHostPathService : public HostPathService
{
    Q_OBJECT

public:
    explicit KioHostPathService(QObject *parent = nullptr);
    ~KioHostPathService() override;

    HostPathState probe(const QString &path) const override;
    bool openDirectory(const QString &path) override;
};

} // namespace Kontainer
