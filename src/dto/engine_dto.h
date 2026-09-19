/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>

namespace Kontainer
{

/*!
 * `GET /version` 的 DTO。
 *
 * ApiVersion / MinAPIVersion 是版本协商的唯一来源（Docker 29 的 /info 已不再返回
 * 这两个字段）。内核版本在 /version 中位于 Components[Name=Engine].Details 下。
 */
/*! `/version` 的 `Components[]` 一项（Engine / containerd / runc / docker-init …）。 */
struct DockerComponentDTO {
    QString name;
    QString version;
};

struct DockerVersionDTO {
    QString version;
    QString apiVersion;
    QString minApiVersion;
    QString os;
    QString arch;
    QString kernelVersion;
    QString gitCommit;
    /*! 引擎自报的组件表：dockerd 之外还有 containerd / runc / docker-init 等。 */
    QList<DockerComponentDTO> components;

    static std::optional<DockerVersionDTO> fromJson(const QJsonObject &object, QString *error = nullptr);
    static std::optional<DockerVersionDTO> fromPayload(const QByteArray &payload, QString *error = nullptr);
};

/*! `GET /info` 的 DTO：Engine 总体信息与容器/镜像计数。 */
struct DockerInfoDTO {
    QString engineName;
    QString operatingSystem;
    QString osType;
    QString architecture;
    QString kernelVersion;
    QString cgroupVersion;
    QString cgroupDriver;
    QString storageDriver;
    int containers = 0;
    int containersRunning = 0;
    int containersPaused = 0;
    int containersStopped = 0;
    int images = 0;
    int cpus = 0;
    qint64 memoryTotalBytes = 0;
    /*! `/info` 的 SecurityOptions（判断是否 rootless：含 `name=rootless`）。 */
    QStringList securityOptions;
    /*! `/info` 的 DockerRootDir。 */
    QString dockerRootDir;
    /*! `/info` 的 LoggingDriver（六期日志功能据此判断能否读取）。 */
    QString loggingDriver;
    /*! `/info` 的 RegistryConfig.Mirrors（配置编辑的「已生效」对照）。 */
    QStringList registryMirrors;
    /*! `/info` 的 Warnings（引擎自己报的配置问题）。 */
    QStringList warnings;
    /*! `/info` 的 LiveRestoreEnabled（重启 daemon 是否影响运行中容器）。 */
    bool liveRestoreEnabled = false;

    static std::optional<DockerInfoDTO> fromJson(const QJsonObject &object, QString *error = nullptr);
    static std::optional<DockerInfoDTO> fromPayload(const QByteArray &payload, QString *error = nullptr);
};

} // namespace Kontainer
