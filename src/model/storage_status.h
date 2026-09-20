/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "domain/storage_usage.h"

#include <QObject>

namespace Kontainer
{

/*!
 * Presentation model for Docker disk usage (ARCH_V2 §23).
 *
 * Engine-level storage stats only; byte counts stay raw and unit conversion is QML's Format job
 * (§17.2). A negative value means that category is unavailable (not in the API) and QML shows
 * "—", not 0.
 */
class StorageStatus : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool buildCacheAvailable READ buildCacheAvailable NOTIFY changed)
    Q_PROPERTY(qint64 imagesBytes READ imagesBytes NOTIFY changed)
    Q_PROPERTY(qint64 containersBytes READ containersBytes NOTIFY changed)
    Q_PROPERTY(qint64 volumesBytes READ volumesBytes NOTIFY changed)
    Q_PROPERTY(qint64 buildCacheBytes READ buildCacheBytes NOTIFY changed)
    Q_PROPERTY(qint64 totalBytes READ totalBytes NOTIFY changed)
    Q_PROPERTY(int imageCount READ imageCount NOTIFY changed)
    Q_PROPERTY(int containerCount READ containerCount NOTIFY changed)
    Q_PROPERTY(int volumeCount READ volumeCount NOTIFY changed)
    Q_PROPERTY(int buildCacheCount READ buildCacheCount NOTIFY changed)

public:
    explicit StorageStatus(QObject *parent = nullptr);

    void setUsage(const StorageUsage &usage);
    void clear();

    bool available() const
    {
        return m_usage.valid;
    }
    bool buildCacheAvailable() const
    {
        return m_usage.buildCacheAvailable;
    }
    qint64 imagesBytes() const
    {
        return m_usage.imagesBytes;
    }
    qint64 containersBytes() const
    {
        return m_usage.containersBytes;
    }
    qint64 volumesBytes() const
    {
        return m_usage.volumesBytes;
    }
    qint64 buildCacheBytes() const
    {
        return m_usage.buildCacheBytes;
    }
    qint64 totalBytes() const
    {
        return m_usage.valid ? m_usage.totalBytes() : -1;
    }
    int imageCount() const
    {
        return m_usage.imageCount;
    }
    int containerCount() const
    {
        return m_usage.containerCount;
    }
    int volumeCount() const
    {
        return m_usage.volumeCount;
    }
    int buildCacheCount() const
    {
        return m_usage.buildCacheCount;
    }

Q_SIGNALS:
    void changed();

private:
    StorageUsage m_usage;
};

} // namespace Kontainer
