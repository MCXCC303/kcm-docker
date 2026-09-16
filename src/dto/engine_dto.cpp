/*
    SPDX-FileCopyrightText: 2026 kontainer developers
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

/*! 从 /version 的 Components 数组中取出 Engine 组件的 Details。 */
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

    // ApiVersion 是版本协商的必要信息，缺失即视为响应不可用
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

    dto.containers = intValue(object, QStringLiteral("Containers"));
    dto.containersRunning = intValue(object, QStringLiteral("ContainersRunning"));
    dto.containersPaused = intValue(object, QStringLiteral("ContainersPaused"));
    dto.containersStopped = intValue(object, QStringLiteral("ContainersStopped"));
    dto.images = intValue(object, QStringLiteral("Images"));
    dto.cpus = intValue(object, QStringLiteral("NCPU"));
    dto.memoryTotalBytes = integerValue(object, QStringLiteral("MemTotal"));

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
