/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "dto/container_inspect_dto.h"

#include "dto/json_helpers.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace Kontainer
{

using namespace JsonHelpers;

namespace
{

QList<DockerPortDTO> parsePorts(const QJsonObject &networkSettings)
{
    QList<DockerPortDTO> ports;
    const QJsonObject portMap = networkSettings.value(QStringLiteral("Ports")).toObject();
    for (auto it = portMap.constBegin(); it != portMap.constEnd(); ++it) {
        // key 形如 "80/tcp" 或 "53/udp"
        const QString key = it.key();
        const int slash = key.indexOf(QLatin1Char('/'));
        DockerPortDTO port;
        port.privatePort = static_cast<quint16>(slash > 0 ? key.left(slash).toUInt() : key.toUInt());
        port.type = slash > 0 ? key.mid(slash + 1) : QString();

        const QJsonValue bindings = it.value();
        if (!bindings.isArray() || bindings.toArray().isEmpty()) {
            ports.append(port); // 未发布到宿主
            continue;
        }
        const QJsonArray array = bindings.toArray();
        for (const QJsonValue &entry : array) {
            if (!entry.isObject()) {
                continue;
            }
            DockerPortDTO published = port;
            const QJsonObject binding = entry.toObject();
            published.ip = stringValue(binding, QStringLiteral("HostIp"));
            const auto hostPort = intValue(binding, QStringLiteral("HostPort"));
            published.publicPort = hostPort > 0 && hostPort <= 65535 ? static_cast<quint16>(hostPort) : 0;
            ports.append(published);
        }
    }
    return ports;
}

QList<ContainerNetworkDTO> parseNetworks(const QJsonObject &networkSettings)
{
    QList<ContainerNetworkDTO> networks;
    const QJsonObject map = networkSettings.value(QStringLiteral("Networks")).toObject();
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
        if (!it.value().isObject()) {
            continue;
        }
        const QJsonObject entry = it.value().toObject();
        ContainerNetworkDTO network;
        network.name = it.key();
        network.networkId = stringValue(entry, QStringLiteral("NetworkID"));
        network.ipAddress = stringValue(entry, QStringLiteral("IPAddress"));
        network.ipv6Address = stringValue(entry, QStringLiteral("GlobalIPv6Address"));
        network.macAddress = stringValue(entry, QStringLiteral("MacAddress"));
        network.gateway = stringValue(entry, QStringLiteral("Gateway"));
        networks.append(network);
    }
    return networks;
}

QList<DockerMountDTO> parseMounts(const QJsonObject &object)
{
    QList<DockerMountDTO> mounts;
    const QJsonValue value = object.value(QStringLiteral("Mounts"));
    if (!value.isArray()) {
        return mounts;
    }
    const QJsonArray array = value.toArray();
    for (const QJsonValue &entry : array) {
        if (!entry.isObject()) {
            continue;
        }
        const QJsonObject mountObject = entry.toObject();
        DockerMountDTO mount;
        mount.type = stringValue(mountObject, QStringLiteral("Type"));
        mount.name = stringValue(mountObject, QStringLiteral("Name"));
        mount.source = stringValue(mountObject, QStringLiteral("Source"));
        mount.destination = stringValue(mountObject, QStringLiteral("Destination"));
        mount.mode = stringValue(mountObject, QStringLiteral("Mode"));
        mount.readWrite = boolValue(mountObject, QStringLiteral("RW"), true);
        mounts.append(mount);
    }
    return mounts;
}

QString restartPolicyText(const QJsonObject &hostConfig)
{
    const QJsonObject policy = hostConfig.value(QStringLiteral("RestartPolicy")).toObject();
    const QString name = stringValue(policy, QStringLiteral("Name"));
    const int retries = intValue(policy, QStringLiteral("MaximumRetryCount"));
    if (name.isEmpty() || name == QLatin1String("no")) {
        return {};
    }
    if (retries > 0) {
        return QStringLiteral("%1:%2").arg(name).arg(retries);
    }
    return name;
}

} // namespace

std::optional<DockerContainerInspectDTO> DockerContainerInspectDTO::fromJson(const QJsonObject &object, QString *error)
{
    DockerContainerInspectDTO dto;

    dto.id = stringValue(object, QStringLiteral("Id"));
    if (dto.id.isEmpty()) {
        if (error) {
            *error = QStringLiteral("container inspect payload without Id");
        }
        return std::nullopt;
    }

    dto.name = stringValue(object, QStringLiteral("Name"));
    if (dto.name.startsWith(QLatin1Char('/'))) {
        dto.name.remove(0, 1);
    }
    // 注意：顶层 Image 是镜像 ID；用户可读的 repository:tag 在 Config.Image
    dto.imageId = stringValue(object, QStringLiteral("Image"));
    dto.image = stringValue(object.value(QStringLiteral("Config")).toObject(), QStringLiteral("Image"));
    if (dto.image.isEmpty()) {
        dto.image = dto.imageId;
    }
    dto.platform = stringValue(object, QStringLiteral("Platform"));
    dto.created = parseDockerTimestamp(stringValue(object, QStringLiteral("Created")));
    dto.restartCount = intValue(object, QStringLiteral("RestartCount"));

    const QJsonObject state = object.value(QStringLiteral("State")).toObject();
    dto.status = stringValue(state, QStringLiteral("Status"));
    dto.running = boolValue(state, QStringLiteral("Running"));
    dto.paused = boolValue(state, QStringLiteral("Paused"));
    dto.restarting = boolValue(state, QStringLiteral("Restarting"));
    dto.dead = boolValue(state, QStringLiteral("Dead"));
    dto.oomKilled = boolValue(state, QStringLiteral("OOMKilled"));
    dto.exitCode = intValue(state, QStringLiteral("ExitCode"));
    dto.pid = intValue(state, QStringLiteral("Pid"));
    dto.startedAt = stringValue(state, QStringLiteral("StartedAt"));
    dto.finishedAt = stringValue(state, QStringLiteral("FinishedAt"));
    const QJsonValue health = state.value(QStringLiteral("Health"));
    if (health.isObject()) {
        dto.healthStatus = stringValue(health.toObject(), QStringLiteral("Status"));
    }

    const QJsonObject config = object.value(QStringLiteral("Config")).toObject();
    dto.environment = stringListValue(config, QStringLiteral("Env"));
    dto.command = stringListValue(config, QStringLiteral("Cmd"));
    dto.entrypoint = stringListValue(config, QStringLiteral("Entrypoint"));
    dto.labels = stringListValue(config, QStringLiteral("Labels"));
    if (dto.labels.isEmpty()) {
        // Labels 也可能是对象形式
        const QJsonObject labelObject = config.value(QStringLiteral("Labels")).toObject();
        for (auto it = labelObject.constBegin(); it != labelObject.constEnd(); ++it) {
            dto.labels.append(it.key() + QLatin1Char('=') + it.value().toString());
        }
    }
    dto.workingDirectory = stringValue(config, QStringLiteral("WorkingDir"));
    dto.user = stringValue(config, QStringLiteral("User"));
    dto.hostname = stringValue(config, QStringLiteral("Hostname"));
    dto.tty = config.value(QStringLiteral("Tty")).toBool(false);

    dto.restartPolicy = restartPolicyText(object.value(QStringLiteral("HostConfig")).toObject());
    dto.ports = parsePorts(object.value(QStringLiteral("NetworkSettings")).toObject());
    dto.networks = parseNetworks(object.value(QStringLiteral("NetworkSettings")).toObject());
    dto.mounts = parseMounts(object);

    return dto;
}

std::optional<DockerContainerInspectDTO> DockerContainerInspectDTO::fromPayload(const QByteArray &payload, QString *error)
{
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (!document.isObject()) {
        if (error) {
            *error = QStringLiteral("container inspect payload is not a JSON object");
        }
        return std::nullopt;
    }
    return fromJson(document.object(), error);
}

ContainerDetail containerDetailFromDto(const DockerContainerInspectDTO &dto)
{
    ContainerDetail detail;
    detail.id = dto.id;
    detail.name = dto.name;
    detail.image = dto.image;
    detail.imageId = dto.imageId;
    detail.platform = dto.platform;
    detail.status = dto.status;
    detail.state = containerStateFromString(dto.status);
    detail.health = healthStateFromString(dto.healthStatus);
    detail.created = dto.created;
    detail.started = parseDockerTimestamp(dto.startedAt);
    detail.finished = parseDockerTimestamp(dto.finishedAt);
    detail.exitCode = dto.exitCode;
    detail.oomKilled = dto.oomKilled;
    detail.restarting = dto.restarting;
    detail.paused = dto.paused;
    detail.dead = dto.dead;
    detail.pid = dto.pid;
    detail.restartCount = dto.restartCount;
    detail.environment = dto.environment;
    detail.command = dto.command;
    detail.entrypoint = dto.entrypoint;
    detail.workingDirectory = dto.workingDirectory;
    detail.user = dto.user;
    detail.hostname = dto.hostname;
    detail.tty = dto.tty;
    detail.restartPolicy = dto.restartPolicy;

    detail.ports.reserve(dto.ports.size());
    for (const DockerPortDTO &portDto : dto.ports) {
        Port port;
        port.ip = portDto.ip;
        port.privatePort = portDto.privatePort;
        port.publicPort = portDto.publicPort;
        port.type = portDto.type;
        detail.ports.append(port);
    }

    detail.networks.reserve(dto.networks.size());
    for (const ContainerNetworkDTO &networkDto : dto.networks) {
        ContainerNetwork network;
        network.name = networkDto.name;
        network.id = networkDto.networkId;
        network.ipAddress = networkDto.ipAddress;
        network.ipv6Address = networkDto.ipv6Address;
        network.macAddress = networkDto.macAddress;
        network.gateway = networkDto.gateway;
        detail.networks.append(network);
    }

    detail.mounts.reserve(dto.mounts.size());
    for (const DockerMountDTO &mountDto : dto.mounts) {
        ContainerMount mount;
        mount.type = mountDto.type;
        mount.name = mountDto.name;
        mount.source = mountDto.source;
        mount.destination = mountDto.destination;
        mount.mode = mountDto.mode;
        mount.readOnly = !mountDto.readWrite;
        detail.mounts.append(mount);
    }

    for (const QString &label : dto.labels) {
        const int equals = label.indexOf(QLatin1Char('='));
        if (equals > 0) {
            detail.labels.append({label.left(equals), label.mid(equals + 1)});
        } else {
            detail.labels.append({label, QString()});
        }
    }

    return detail;
}

} // namespace Kontainer
