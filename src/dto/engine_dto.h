/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
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
 * DTO for `GET /version`.
 *
 * ApiVersion / MinAPIVersion are the only source for version negotiation (Docker 29's /info no
 * longer returns them). The kernel version lives under Components[Name=Engine].Details.
 */
/*! One `Components[]` entry of `/version` (Engine / containerd / runc / docker-init …). */
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
    /*! Component table the engine reports: beyond dockerd, containerd / runc / docker-init etc. */
    QList<DockerComponentDTO> components;

    static std::optional<DockerVersionDTO> fromJson(const QJsonObject &object, QString *error = nullptr);
    static std::optional<DockerVersionDTO> fromPayload(const QByteArray &payload, QString *error = nullptr);
};

/*! DTO for `GET /info`: overall engine information plus container/image counts. */
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
    /*! `/info` SecurityOptions (rootless check: contains `name=rootless`). */
    QStringList securityOptions;
    /*! `/info` DockerRootDir. */
    QString dockerRootDir;
    /*! `/info` LoggingDriver (the phase-6 log feature uses it to decide readability). */
    QString loggingDriver;
    /*! `/info` RegistryConfig.Mirrors (the "in effect" reference for config editing). */
    QStringList registryMirrors;
    /*! `/info` Warnings (configuration problems the engine itself reports). */
    QStringList warnings;
    /*! `/info` LiveRestoreEnabled (whether a daemon restart disturbs running containers). */
    bool liveRestoreEnabled = false;

    static std::optional<DockerInfoDTO> fromJson(const QJsonObject &object, QString *error = nullptr);
    static std::optional<DockerInfoDTO> fromPayload(const QByteArray &payload, QString *error = nullptr);
};

} // namespace Kontainer
