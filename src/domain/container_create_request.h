/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

namespace Kontainer
{

/*!
 * 一条端口映射（创建表单用；与四期只读的 `PortMappingEntry` 区分开）。
 *
 * `hostPort == 0` 表示**随机分配**（Docker 会挑一个空闲端口）：这是常见需求，
 * 因此用 0 而不是空字符串表达。
 */
struct ContainerPortRequest {
    QString hostIp;   /*!< 留空 = 引擎默认（0.0.0.0） */
    quint16 hostPort = 0;
    quint16 containerPort = 0;
    QString protocol = QStringLiteral("tcp");
};

/*!
 * 一条挂载请求（bind / volume / tmpfs 三种，字段含义随类型变化）。
 */
struct ContainerMountRequest {
    /*! bind / volume / tmpfs */
    QString type = QStringLiteral("bind");
    /*! bind 的宿主路径；volume 的卷名；tmpfs 为空。 */
    QString source;
    /*! 容器内路径（必填，绝对路径）。 */
    QString destination;
    bool readOnly = false;
};

/*!
 * 创建容器的请求（ARCH_V5_V8 §4.4/§4.6）。
 *
 * 这是**表单字段到 Docker API 的唯一映射点**：`toJson()` 是纯函数，
 * 因此"填的表单会不会变成正确的请求"可以用快照断言钉死，而不是靠注释描述。
 *
 * 只覆盖七期表单真正会填的字段：命令/入口点/环境/标签/端口/挂载/网络/重启策略/
 * 内存与 CPU/privileged。其余（如 devices、sysctls、健康检查）留给需要时再加，
 * 不预先堆一堆没人用的字段。
 */
struct ContainerCreateRequest {
    QString name;
    QString image;
    /*! 覆盖镜像的 Cmd（空 = 用镜像的）。 */
    QStringList command;
    /*! 覆盖镜像的 Entrypoint（空 = 用镜像的）。 */
    QStringList entrypoint;
    QStringList environment; /*!< `KEY=value` 形式，与 Docker API 一致 */
    QList<QPair<QString, QString>> labels;
    QString workingDirectory;
    QString user;
    QString hostname;
    /*! 容器内要暴露的端口（`EXPOSE`），例如 `80/tcp`。 */
    QStringList exposedPorts;
    QList<ContainerPortRequest> ports;
    QList<ContainerMountRequest> mounts;
    /*! 网络名（空 = 不指定，引擎用默认 bridge）。 */
    QString network;
    /*! 网络别名（仅在该网络里有效）。 */
    QStringList networkAliases;
    /*! no / always / unless-stopped / on-failure。 */
    QString restartPolicy = QStringLiteral("no");
    int restartMaxRetries = 0;
    /*! 内存上限（字节）；0 = 不限制。 */
    qint64 memoryLimitBytes = 0;
    /*! CPU 核数（例如 1.5）；0 = 不限制。 */
    double cpus = 0.0;
    /*!
     * 特权容器（等同宿主 root）。
     *
     * 表单里必须二次确认——这是整个创建流程里唯一能直接拿到宿主 root 的开关。
     */
    bool privileged = false;
    /*!
     * 交互能力（对应 `docker run -i -t`）。
     *
     * 用户实测：只用默认参数创建的 alpine 容器"启动后立刻退出"——因为镜像的默认命令
     * （`/bin/sh`）在没有 stdin、没有 TTY 时读到 EOF 就退出。默认开 `-i -t` 才是
     * "手动调试容器"的常见预期（也是 docker CLI 常用的组合）。
     */
    bool openStdin = false;
    bool tty = false;
    /*! `--stdin-once`：stdin 只保持到第一个客户端断开（配合 `-i` 用）。 */
    bool stdinOnce = false;

    /*! 创建后立即启动（界面上的「创建并启动」）。 */
    bool startAfterCreate = false;

    /*!
     * 请求体 JSON（`POST /containers/create`）。
     *
     * 键名与 Docker API 一致：`HostConfig.Binds` / `PortBindings` / `RestartPolicy` /
     * `Memory` / `NanoCpus` / `Privileged`，网络走 `NetworkingConfig.EndpointsConfig`。
     * 空字段不写进 JSON（让引擎用它自己的默认值）。
     */
    QByteArray toJson() const;
    /*! 创建请求的 query（`name` 是 query 参数，不是体）。 */
    QString queryString() const;
};

/*!
 * 各字段的校验（ARCH_V5_V8 §4.3 的校验矩阵）。
 *
 * 返回**稳定的错误 key**（空 = 通过），文案在 QML 侧——与项目其它校验一致。
 * 需要"对照现有容器"的检查（重名、端口冲突）由控制器做，因为它们依赖后端数据。
 */
QString validateContainerName(const QString &name);
QString validateContainerPath(const QString &path);
/*! 环境变量/标签键：`[A-Za-z_][A-Za-z0-9_]*`。 */
QString validateEnvironmentKey(const QString &key);
QString validateHostPort(quint16 port);
QString validateMemoryLimit(qint64 bytes);
QString validateCpus(double cpus);

} // namespace Kontainer
