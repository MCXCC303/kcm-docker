/*
    SPDX-FileCopyrightText: 2026 kontainer developers
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
/*! `80/tcp` / `80` → `80/tcp`（Docker 的 ExposedPorts 用这种键）。 */
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

    // ExposedPorts：显式 EXPOSE 的端口 + 端口映射里的容器端口（Docker 会把后者也当成暴露）
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

    // 挂载：bind 与命名卷都走 Binds（`source:destination[:ro]`），语义与 docker CLI 一致。
    // tmpfs 不能进 Binds（它不是路径映射），走 Tmpfs 映射。
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
            continue; // 没有来源的 bind/volume 会被引擎拒绝，这里直接不发
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

    // 端口映射：PortBindings 是 "容器端口/协议" → [{HostIp, HostPort}]
    if (!ports.isEmpty()) {
        QJsonObject bindings;
        for (const ContainerPortRequest &port : ports) {
            if (port.containerPort == 0) {
                continue;
            }
            QJsonObject entry;
            entry.insert(QStringLiteral("HostIp"), port.hostIp);
            // 0 表示随机分配：Docker 用空字符串表达"随机"
            entry.insert(QStringLiteral("HostPort"), port.hostPort == 0 ? QString() : QString::number(port.hostPort));
            QJsonArray array;
            array.append(entry);
            bindings.insert(portKey(port.containerPort, port.protocol), array);
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
        // Docker 用"十亿分之一核"的整数表达 CPU：1.5 核 = 1_500_000_000
        hostConfig.insert(QStringLiteral("NanoCpus"), double(qint64(cpus * 1'000'000'000.0)));
    }
    if (privileged) {
        hostConfig.insert(QStringLiteral("Privileged"), true);
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
    // name 是 query 参数（不是请求体）：这一点与"看起来像字段"的直觉相反，写在这里并有用例守着
    query.addQueryItem(QStringLiteral("name"), name);
    return query.toString(QUrl::FullyEncoded);
}

QString validateContainerName(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return QStringLiteral("nameRequired");
    }
    // Docker 的容器名规则（与网络/卷一致）
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
    // 0 是"随机分配"，不是错误
    Q_UNUSED(port);
    return {};
}

QString validateMemoryLimit(qint64 bytes)
{
    if (bytes < 0) {
        return QStringLiteral("memoryNegative");
    }
    // Docker 的最小内存限制是 6 MiB（低于它引擎会拒绝，提前挡住能省一次往返）
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
