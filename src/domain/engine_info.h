/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QString>

namespace Kontainer
{

/*!
 * Engine 域数据（ARCH_V2 §25/§26：domain object 是 Kontainer 自己的稳定语义，
 * 既不是 Docker API 的 DTO 镜像，也不含任何 UI 文案）。
 */
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
};

} // namespace Kontainer
