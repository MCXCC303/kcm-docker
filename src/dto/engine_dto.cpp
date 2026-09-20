/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "dto/engine_dto.h"

#include "dto/json_helpers.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace Kontainer
{

using namespace JsonHelpers;

namespace
{

std::optional<QJsonObject> objectFromPayload(const QByteArray &payload, QString *error)
{
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (!document.isObject()) {
        if (error) {
            *error = QStringLiteral("payload is not a JSON object");
        }
        return std::nullopt;
    }
    return document.object();
}

/*! `/version` `Components[]` → component table (Name + Version). */
QList<DockerComponentDTO> parseComponents(const QJsonObject &object)
{
    QList<DockerComponentDTO> components;
    const QJsonArray array = object.value(QStringLiteral("Components")).toArray();
    components.reserve(array.size());
    for (const QJsonValue &entry : array) {
        if (!entry.isObject()) {
            continue;
        }
        const QJsonObject component = entry.toObject();
        DockerComponentDTO dto;
        dto.name = stringValue(component, QStringLiteral("Name"));
        dto.version = stringValue(component, QStringLiteral("Version"));
        if (!dto.name.isEmpty()) {
            components.append(dto);
        }
    }
    return components;
}

/*! Pull the Engine component's Details out of the /version Components array. */
QJsonObject engineComponentDetails(const QJsonObject &object)
{
    const QJsonArray components = object.value(QStringLiteral("Components")).toArray();
    for (const QJsonValue &entry : components) {
        if (!entry.isObject()) {
            continue;
        }
        const QJsonObject component = entry.toObject();
        if (stringValue(component, QStringLiteral("Name")) == QLatin1String("Engine")) {
            return component.value(QStringLiteral("Details")).toObject();
        }
    }
    return {};
}

} // namespace

std::optional<DockerVersionDTO> DockerVersionDTO::fromJson(const QJsonObject &object, QString *error)
{
    DockerVersionDTO dto;
    dto.version = stringValue(object, QStringLiteral("Version"));
    dto.apiVersion = stringValue(object, QStringLiteral("ApiVersion"));
    dto.minApiVersion = stringValue(object, QStringLiteral("MinAPIVersion"));
    dto.os = stringValue(object, QStringLiteral("Os"));
    dto.arch = stringValue(object, QStringLiteral("Arch"));
    dto.components = parseComponents(object);

    const QJsonObject details = engineComponentDetails(object);
    if (!details.isEmpty()) {
        dto.kernelVersion = stringValue(details, QStringLiteral("KernelVersion"));
        dto.gitCommit = stringValue(details, QStringLiteral("GitCommit"));
        if (dto.apiVersion.isEmpty()) {
            dto.apiVersion = stringValue(details, QStringLiteral("ApiVersion"));
        }
        if (dto.minApiVersion.isEmpty()) {
            dto.minApiVersion = stringValue(details, QStringLiteral("MinAPIVersion"));
        }
    }

    // ApiVersion is required for version negotiation; without it the response is unusable
    if (dto.apiVersion.isEmpty()) {
        if (error) {
            *error = QStringLiteral("version payload without ApiVersion");
        }
        return std::nullopt;
    }
    return dto;
}

std::optional<DockerVersionDTO> DockerVersionDTO::fromPayload(const QByteArray &payload, QString *error)
{
    const auto object = objectFromPayload(payload, error);
    if (!object.has_value()) {
        return std::nullopt;
    }
    return fromJson(*object, error);
}

std::optional<DockerInfoDTO> DockerInfoDTO::fromJson(const QJsonObject &object, QString *error)
{
    Q_UNUSED(error)

    DockerInfoDTO dto;
    dto.engineName = stringValue(object, QStringLiteral("Name"));
    dto.operatingSystem = stringValue(object, QStringLiteral("OperatingSystem"));
    dto.osType = stringValue(object, QStringLiteral("OSType"));
    dto.architecture = stringValue(object, QStringLiteral("Architecture"));
    dto.kernelVersion = stringValue(object, QStringLiteral("KernelVersion"));
    dto.cgroupVersion = stringValue(object, QStringLiteral("CgroupVersion"));
    dto.cgroupDriver = stringValue(object, QStringLiteral("CgroupDriver"));
    dto.storageDriver = stringValue(object, QStringLiteral("Driver"));

    dto.warnings = stringListValue(object, QStringLiteral("Warnings"));
    dto.containers = intValue(object, QStringLiteral("Containers"));
    dto.containersRunning = intValue(object, QStringLiteral("ContainersRunning"));
    dto.containersPaused = intValue(object, QStringLiteral("ContainersPaused"));
    dto.containersStopped = intValue(object, QStringLiteral("ContainersStopped"));
    dto.images = intValue(object, QStringLiteral("Images"));
    dto.cpus = intValue(object, QStringLiteral("NCPU"));
    dto.memoryTotalBytes = integerValue(object, QStringLiteral("MemTotal"));

        const QJsonValue security = object.value(QStringLiteral("SecurityOptions"));
        if (security.isArray()) {
            for (const QJsonValue &entry : security.toArray()) {
                if (entry.isString()) {
                    dto.securityOptions.append(entry.toString());
                }
            }
        }
        dto.dockerRootDir = stringValue(object, QStringLiteral("DockerRootDir"));
        dto.loggingDriver = stringValue(object, QStringLiteral("LoggingDriver"));
        dto.liveRestoreEnabled = boolValue(object, QStringLiteral("LiveRestoreEnabled"));

        const QJsonValue registryConfig = object.value(QStringLiteral("RegistryConfig"));
        if (registryConfig.isObject()) {
            const QJsonValue mirrors = registryConfig.toObject().value(QStringLiteral("Mirrors"));
            if (mirrors.isArray()) {
                for (const QJsonValue &entry : mirrors.toArray()) {
                    if (entry.isString()) {
                        dto.registryMirrors.append(entry.toString());
                    }
                }
            }
        }

    return dto;
}

std::optional<DockerInfoDTO> DockerInfoDTO::fromPayload(const QByteArray &payload, QString *error)
{
    const auto object = objectFromPayload(payload, error);
    if (!object.has_value()) {
        return std::nullopt;
    }
    return fromJson(*object, error);
}

} // namespace Kontainer
