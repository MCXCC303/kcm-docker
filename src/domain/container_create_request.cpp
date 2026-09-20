/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "domain/container_create_request.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

namespace Kontainer
{

namespace
{
/*! `80/tcp` / `80` → `80/tcp` (the key form Docker uses for ExposedPorts). */
QString portKey(quint16 port, const QString &protocol)
{
    const QString proto = protocol.isEmpty() ? QStringLiteral("tcp") : protocol;
    return QStringLiteral("%1/%2").arg(port).arg(proto);
}
} // namespace

QByteArray ContainerCreateRequest::toJson() const
{
    QJsonObject root;
    root.insert(QStringLiteral("Image"), image);

    if (!command.isEmpty()) {
        QJsonArray array;
        for (const QString &item : command) {
            array.append(item);
        }
        root.insert(QStringLiteral("Cmd"), array);
    }
    if (!entrypoint.isEmpty()) {
        QJsonArray array;
        for (const QString &item : entrypoint) {
            array.append(item);
        }
        root.insert(QStringLiteral("Entrypoint"), array);
    }
    if (!environment.isEmpty()) {
        QJsonArray array;
        for (const QString &item : environment) {
            array.append(item);
        }
        root.insert(QStringLiteral("Env"), array);
    }
    if (!labels.isEmpty()) {
        QJsonObject object;
        for (const auto &label : labels) {
            if (!label.first.isEmpty()) {
                object.insert(label.first, label.second);
            }
        }
        root.insert(QStringLiteral("Labels"), object);
    }
    if (!workingDirectory.isEmpty()) {
        root.insert(QStringLiteral("WorkingDir"), workingDirectory);
    }
    if (!user.isEmpty()) {
        root.insert(QStringLiteral("User"), user);
    }
    if (!hostname.isEmpty()) {
        root.insert(QStringLiteral("Hostname"), hostname);
    }

    // ExposedPorts: explicitly EXPOSEd ports plus mapped container ports (Docker exposes those too)
    QStringList exposed = exposedPorts;
    for (const ContainerPortRequest &port : ports) {
        const QString key = portKey(port.containerPort, port.protocol);
        if (!exposed.contains(key)) {
            exposed.append(key);
        }
    }
    if (!exposed.isEmpty()) {
        QJsonObject object;
        for (const QString &key : exposed) {
            object.insert(key, QJsonObject());
        }
        root.insert(QStringLiteral("ExposedPorts"), object);
    }

    /* ---------------- HostConfig ---------------- */
    QJsonObject hostConfig;

    // Mounts: bind and named volumes both go through Binds (`source:destination[:ro]`), same
    // semantics as the docker CLI. tmpfs cannot go into Binds (it is no path mapping) and uses Tmpfs.
    QStringList binds;
    QJsonObject tmpfs;
    for (const ContainerMountRequest &mount : mounts) {
        if (mount.destination.isEmpty()) {
            continue;
        }
        if (mount.type == QLatin1String("tmpfs")) {
            tmpfs.insert(mount.destination, QString());
            continue;
        }
        if (mount.source.isEmpty()) {
            continue; // the engine rejects a bind/volume without a source, so never send it
        }
        QString bind = mount.source + QLatin1Char(':') + mount.destination;
        if (mount.readOnly) {
            bind += QStringLiteral(":ro");
        }
        binds.append(bind);
    }
    if (!binds.isEmpty()) {
        QJsonArray array;
        for (const QString &bind : binds) {
            array.append(bind);
        }
        hostConfig.insert(QStringLiteral("Binds"), array);
    }
    if (!tmpfs.isEmpty()) {
        hostConfig.insert(QStringLiteral("Tmpfs"), tmpfs);
    }

    // Port mappings: PortBindings maps "containerPort/protocol" → [{HostIp, HostPort}]
    if (!ports.isEmpty()) {
        QJsonObject bindings;
        for (const ContainerPortRequest &port : ports) {
            if (port.containerPort == 0) {
                continue;
            }
            QJsonObject entry;
            entry.insert(QStringLiteral("HostIp"), port.hostIp);
            // 0 means random assignment: Docker expresses "random" as an empty string
            entry.insert(QStringLiteral("HostPort"), port.hostPort == 0 ? QString() : QString::number(port.hostPort));
            /*
             * One container port may map to **several host ports** (reported in testing: mapping
             * 1000/2000/3000 to container 80 left only 1000 active). PortBindings is
             * "container port → array of bindings", so entries must be **appended**; a plain insert
             * would keep only the last one.
             */
            const QString key = portKey(port.containerPort, port.protocol);
            QJsonArray array = bindings.value(key).toArray();
            array.append(entry);
            bindings.insert(key, array);
        }
        if (!bindings.isEmpty()) {
            hostConfig.insert(QStringLiteral("PortBindings"), bindings);
        }
    }

    if (restartPolicy != QLatin1String("no") || restartMaxRetries > 0) {
        QJsonObject policy;
        policy.insert(QStringLiteral("Name"), restartPolicy);
        if (restartPolicy == QLatin1String("on-failure") && restartMaxRetries > 0) {
            policy.insert(QStringLiteral("MaximumRetryCount"), restartMaxRetries);
        }
        hostConfig.insert(QStringLiteral("RestartPolicy"), policy);
    }
    if (memoryLimitBytes > 0) {
        hostConfig.insert(QStringLiteral("Memory"), double(memoryLimitBytes));
    }
    if (cpus > 0.0) {
        // Docker expresses CPU in billionths of a core: 1.5 cores = 1_500_000_000
        hostConfig.insert(QStringLiteral("NanoCpus"), double(qint64(cpus * 1'000'000'000.0)));
    }
    if (privileged) {
        hostConfig.insert(QStringLiteral("Privileged"), true);
    }
    // Interactivity lives at the **top level** (it is not part of HostConfig)
    if (openStdin) {
        root.insert(QStringLiteral("OpenStdin"), true);
    }
    if (tty) {
        root.insert(QStringLiteral("Tty"), true);
    }
    if (stdinOnce) {
        root.insert(QStringLiteral("StdinOnce"), true);
    }
    if (!hostConfig.isEmpty()) {
        root.insert(QStringLiteral("HostConfig"), hostConfig);
    }

    /* ---------------- NetworkingConfig ---------------- */
    if (!network.isEmpty()) {
        QJsonObject endpoint;
        if (!networkAliases.isEmpty()) {
            QJsonArray aliases;
            for (const QString &alias : networkAliases) {
                if (!alias.trimmed().isEmpty()) {
                    aliases.append(alias.trimmed());
                }
            }
            endpoint.insert(QStringLiteral("Aliases"), aliases);
        }
        QJsonObject endpoints;
        endpoints.insert(network, endpoint);
        QJsonObject networking;
        networking.insert(QStringLiteral("EndpointsConfig"), endpoints);
        root.insert(QStringLiteral("NetworkingConfig"), networking);
    }

    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QString ContainerCreateRequest::queryString() const
{
    QUrlQuery query;
    // name is a query parameter, not part of the body — counter-intuitive, so a test guards it
    query.addQueryItem(QStringLiteral("name"), name);
    return query.toString(QUrl::FullyEncoded);
}

QString validateContainerName(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return QStringLiteral("nameRequired");
    }
    // Docker's container name rules (same as for networks/volumes)
    static const QRegularExpression allowed(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.-]*$"));
    if (!allowed.match(trimmed).hasMatch()) {
        return QStringLiteral("nameInvalid");
    }
    return {};
}

QString validateContainerPath(const QString &path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty()) {
        return QStringLiteral("pathRequired");
    }
    if (!trimmed.startsWith(QLatin1Char('/'))) {
        return QStringLiteral("pathNotAbsolute");
    }
    return {};
}

QString validateEnvironmentKey(const QString &key)
{
    const QString trimmed = key.trimmed();
    if (trimmed.isEmpty()) {
        return QStringLiteral("keyRequired");
    }
    static const QRegularExpression allowed(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    if (!allowed.match(trimmed).hasMatch()) {
        return QStringLiteral("keyInvalid");
    }
    return {};
}

QString validateHostPort(quint16 port)
{
    // 0 means "random assignment", not an error
    Q_UNUSED(port);
    return {};
}

QString validateMemoryLimit(qint64 bytes)
{
    if (bytes < 0) {
        return QStringLiteral("memoryNegative");
    }
    // Docker's minimum memory limit is 6 MiB (below it the engine refuses; catching it saves a trip)
    if (bytes > 0 && bytes < 6 * 1024 * 1024) {
        return QStringLiteral("memoryTooSmall");
    }
    return {};
}

QString validateCpus(double cpus)
{
    if (cpus < 0.0) {
        return QStringLiteral("cpusNegative");
    }
    return {};
}

} // namespace Kontainer
