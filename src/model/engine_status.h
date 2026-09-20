/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "domain/engine_info.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace Kontainer
{

/*!
 * Presentation model for engine status: exposes EngineInfo as QML-bindable properties.
 * Data only; contains no Docker access logic.
 */
class EngineStatus : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool countsAvailable READ countsAvailable NOTIFY changed)
    Q_PROPERTY(QString serverVersion READ serverVersion NOTIFY changed)
    Q_PROPERTY(QString apiVersion READ apiVersion NOTIFY changed)
    Q_PROPERTY(QString minApiVersion READ minApiVersion NOTIFY changed)
    Q_PROPERTY(QString osType READ osType NOTIFY changed)
    Q_PROPERTY(QString architecture READ architecture NOTIFY changed)
    Q_PROPERTY(QString kernelVersion READ kernelVersion NOTIFY changed)
    Q_PROPERTY(QString engineName READ engineName NOTIFY changed)
    /*! Whether this is a rootless deployment (`/info` SecurityOptions contains `name=rootless`). */
    Q_PROPERTY(bool rootless READ rootless NOTIFY changed)
    /*! `/info`'s DockerRootDir. */
    Q_PROPERTY(QString dockerRootDir READ dockerRootDir NOTIFY changed)
    /*! `/info`'s LoggingDriver (phase-six logs use it to decide whether reading is possible). */
    Q_PROPERTY(QString loggingDriver READ loggingDriver NOTIFY changed)
    /*! Registry mirrors reported by `/info` (the "currently in effect" baseline for config edits). */
    Q_PROPERTY(QStringList activeRegistryMirrors READ activeRegistryMirrors NOTIFY changed)
    /*! Whether LiveRestore is on (decides if a daemon restart kills running containers). */
    Q_PROPERTY(bool liveRestoreEnabled READ liveRestoreEnabled NOTIFY changed)
    Q_PROPERTY(QString operatingSystem READ operatingSystem NOTIFY changed)
    Q_PROPERTY(QString cgroupVersion READ cgroupVersion NOTIFY changed)
    /*! `/info`'s CgroupDriver (`systemd` / `cgroupfs`). */
    Q_PROPERTY(QString cgroupDriver READ cgroupDriver NOTIFY changed)
    /*! `/info`'s NCPU. */
    Q_PROPERTY(int cpuCount READ cpuCount NOTIFY changed)
    /*!
     * `/version` component table: `[{name, version}]` (dockerd / containerd / runc / docker-init …).
     *
     * A **property**, not a function: in QML function calls build no dependency (hit repeatedly here).
     */
    Q_PROPERTY(QVariantList components READ components NOTIFY changed)
    /*! `/info` Warnings: configuration problems the engine reports (e.g. swap limit, bridge disabled). */
    Q_PROPERTY(QStringList warnings READ warnings NOTIFY changed)
    Q_PROPERTY(QString storageDriver READ storageDriver NOTIFY changed)
    Q_PROPERTY(int containerTotal READ containerTotal NOTIFY changed)
    Q_PROPERTY(int containersRunning READ containersRunning NOTIFY changed)
    Q_PROPERTY(int containersPaused READ containersPaused NOTIFY changed)
    Q_PROPERTY(int containersStopped READ containersStopped NOTIFY changed)
    Q_PROPERTY(int imageCount READ imageCount NOTIFY changed)

public:
    explicit EngineStatus(QObject *parent = nullptr);

    void setInfo(const EngineInfo &info);
    /*! Clear (called when Docker is unavailable) so the UI never shows stale data. */
    void clear();

    const EngineInfo &info() const
    {
        return m_info;
    }

    bool available() const
    {
        return m_info.available;
    }
    bool countsAvailable() const
    {
        return m_info.countsAvailable;
    }
    QString serverVersion() const
    {
        return m_info.serverVersion;
    }
    QString apiVersion() const
    {
        return m_info.apiVersion;
    }
    QString minApiVersion() const
    {
        return m_info.minApiVersion;
    }
    QString osType() const
    {
        return m_info.osType;
    }
    QString architecture() const
    {
        return m_info.architecture;
    }
    QString kernelVersion() const
    {
        return m_info.kernelVersion;
    }
    QString engineName() const
    {
        return m_info.engineName;
    }
    bool rootless() const;
    QString dockerRootDir() const
    {
        return m_info.dockerRootDir;
    }
    QString loggingDriver() const
    {
        return m_info.loggingDriver;
    }
    QStringList activeRegistryMirrors() const
    {
        return m_info.registryMirrors;
    }
    bool liveRestoreEnabled() const
    {
        return m_info.liveRestoreEnabled;
    }
    QString operatingSystem() const
    {
        return m_info.operatingSystem;
    }
    QString cgroupVersion() const
    {
        return m_info.cgroupVersion;
    }

    QString cgroupDriver() const
    {
        return m_info.cgroupDriver;
    }
    int cpuCount() const
    {
        return m_info.cpuCount;
    }
    QVariantList components() const
    {
        QVariantList list;
        list.reserve(m_info.components.size());
        for (const EngineComponent &component : m_info.components) {
            QVariantMap entry;
            entry.insert(QStringLiteral("name"), component.name);
            entry.insert(QStringLiteral("version"), component.version);
            list.append(entry);
        }
        return list;
    }
    QStringList warnings() const
    {
        return m_info.warnings;
    }
    QString storageDriver() const
    {
        return m_info.storageDriver;
    }
    int containerTotal() const
    {
        return m_info.containerTotal;
    }
    int containersRunning() const
    {
        return m_info.containersRunning;
    }
    int containersPaused() const
    {
        return m_info.containersPaused;
    }
    int containersStopped() const
    {
        return m_info.containersStopped;
    }
    int imageCount() const
    {
        return m_info.imageCount;
    }

Q_SIGNALS:
    /*! Any property changed; QML bindings re-evaluate automatically. */
    void changed();

private:
    EngineInfo m_info;
};

} // namespace Kontainer
