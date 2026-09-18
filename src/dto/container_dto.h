/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "domain/container.h"
#include "dto/container_network_dto.h"

#include <QJsonObject>
#include <QList>
#include <QString>

#include <optional>

namespace Kontainer
{

/*! Docker Engine API 的端口映射条目（/containers/json → Ports[]）。 */
struct DockerPortDTO {
    QString ip;
    quint16 privatePort = 0;
    quint16 publicPort = 0; /*!< 0 表示未发布到宿主 */
    QString type; /*!< tcp / udp / sctp */
};

/*!
 * `GET /containers/json` 的单条记录（ARCH_V1 §40：DTO 是 API schema 的镜像）。
 *
 * 只有 Id / State / Names 是必需的；其余字段缺失或类型不符时使用默认值，
 * 单条记录损坏不会让整个列表失败。
 */
struct DockerContainerDTO {
    QString id;
    QString name;
    QString image;
    QString imageId;
    QString state;
    QString status;
    QString healthStatus; /*!< Docker 29+ 的 Health.Status；老引擎缺失时为空 */
    qint64 createdUnix = 0;
    QList<DockerPortDTO> ports;
    /*! `NetworkSettings.Networks`（网络成员列表的唯一可靠来源，见 domain/container.h 的说明）。 */
    QList<ContainerNetworkDTO> networks;

    /*! 解析单条记录；缺少必需字段时返回 nullopt 并写入 error。 */
    static std::optional<DockerContainerDTO> fromJson(const QJsonObject &object, QString *error = nullptr);

    /*!
     * 解析 `/containers/json` 的完整响应。
     * 非法 JSON 或不是数组 → 返回空列表并写入 error（调用方转 UnexpectedPayload）。
     * 个别记录损坏 → 跳过该记录，skipped 计数递增，其余照常返回。
     */
    static QList<DockerContainerDTO> listFromJson(const QByteArray &payload, QString *error = nullptr, int *skipped = nullptr);
};

/*!
 * DTO → domain object 的映射放在 DTO 层（ARCH_V2 §26）：
 * 方向始终是 dto → domain，domain 不反向依赖 API schema。
 */
Container containerFromDto(const DockerContainerDTO &dto);
QList<Container> containersFromDto(const QList<DockerContainerDTO> &dtos);

} // namespace Kontainer
