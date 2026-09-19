/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace Kontainer
{

/*!
 * Engine 域数据（ARCH_V2 §25/§26：domain object 是 Kontainer 自己的稳定语义，
 * 既不是 Docker API 的 DTO 镜像，也不含任何 UI 文案）。
 */
/*!
 * `/version` 的 `Components[]` 里的一项（Engine / containerd / runc / docker-init …）。
 *
 * 引擎版本只说明 dockerd 自己；containerd、runc 的版本同样影响行为
 * （例如 cgroup v2、镜像格式支持），因此一并展示。
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
    bool available = false; /*!< /_ping 与 /version 成功 */
    bool countsAvailable = false; /*!< /info 成功（容器/镜像计数可信） */
    QString serverVersion;
    QString apiVersion; /*!< 协商后实际使用的客户端 API 版本 */
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
    /*! 宿主内存总量（/info 的 MemTotal）：用于判断容器 memory limit 是否为“无限制”。 */
    qint64 memoryTotalBytes = 0;
    /*! `/info` 的 SecurityOptions：含 `name=rootless` 即为 rootless 部署。 */
    QStringList securityOptions;
    /*! `/info` 的 DockerRootDir。 */
    QString dockerRootDir;
    /*! `/info` 的 LoggingDriver。 */
    QString loggingDriver;
    /*! `/info` 的 RegistryConfig.Mirrors：与配置文件对照可判断「已生效 / 待重启」。 */
    QStringList registryMirrors;
    /*! `/info` 的 LiveRestoreEnabled：为真时重启 daemon 不会停掉运行中的容器。 */
    bool liveRestoreEnabled = false;
    /*! `/version` 的 `Components[]`：dockerd / containerd / runc 等的版本。 */
    QList<EngineComponent> components;
    /*! `/info` 的 CgroupDriver（`systemd` / `cgroupfs`）。 */
    QString cgroupDriver;
    /*! `/info` 的 NCPU。 */
    int cpuCount = 0;
    /*! `/info` 的 Warnings：引擎自己报的配置问题，值得原样展示。 */
    QStringList warnings;
};

} // namespace Kontainer
