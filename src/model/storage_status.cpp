/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/storage_status.h"

namespace Kontainer
{

StorageStatus::StorageStatus(QObject *parent)
    : QObject(parent)
{
}

void StorageStatus::setUsage(const StorageUsage &usage)
{
    m_usage = usage;
    Q_EMIT changed();
}

void StorageStatus::clear()
{
    m_usage = StorageUsage();
    Q_EMIT changed();
}

} // namespace Kontainer
