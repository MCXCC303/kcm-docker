/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace Kontainer
{

/*!
 * Engine domain data (ARCH_V2 §25/§26: a domain object is Kontainer's own stable semantics —
 * not a DTO mirror of the Docker API, and free of UI text).
 */
/*!
 * One entry of `/version`'s `Components[]` (Engine / containerd / runc / docker-init …).
 *
 * The engine version only describes dockerd itself; containerd and runc versions affect
 * behavior too (cgroup v2, image format support), so they are shown as well.
 */
struct EngineComponent {
    QString name;
    QString version;

    bool operator==(const EngineComponent &other) const
    {
        return name == other.name && version == other.version;
    }
};

struct EngineInfo {
    bool available = false; /*!< /_ping and /version succeeded */
    bool countsAvailable = false; /*!< /info succeeded (container/image counts are trustworthy) */
    QString serverVersion;
    QString apiVersion; /*!< client API version actually used after negotiation */
    QString minApiVersion;
    QString osType;
    QString architecture;
    QString kernelVersion;
    QString engineName;
    QString operatingSystem;
    QString cgroupVersion;
    QString storageDriver;
    int containerTotal = 0;
    int containersRunning = 0;
    int containersPaused = 0;
    int containersStopped = 0;
    int imageCount = 0;
    /*! Host total memory (/info MemTotal): tells whether a container's memory limit is "unlimited". */
    qint64 memoryTotalBytes = 0;
    /*! /info SecurityOptions: containing `name=rootless` means a rootless deployment. */
    QStringList securityOptions;
    /*! /info DockerRootDir. */
    QString dockerRootDir;
    /*! /info LoggingDriver. */
    QString loggingDriver;
    /*! /info RegistryConfig.Mirrors: comparing with the config file shows "applied / needs restart". */
    QStringList registryMirrors;
    /*! /info LiveRestoreEnabled: when true, restarting the daemon keeps running containers alive. */
    bool liveRestoreEnabled = false;
    /*! /version `Components[]`: versions of dockerd / containerd / runc etc. */
    QList<EngineComponent> components;
    /*! /info CgroupDriver (`systemd` / `cgroupfs`). */
    QString cgroupDriver;
    /*! /info NCPU. */
    int cpuCount = 0;
    /*! /info Warnings: configuration problems reported by the engine; worth showing verbatim. */
    QStringList warnings;
};

} // namespace Kontainer
