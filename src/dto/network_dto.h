/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "domain/network.h"

#include <QJsonObject>
#include <QList>
#include <QString>

#include <optional>

namespace Kontainer
{

/*!
 * `GET /networks` 的单条记录（ARCH_V5_V8 §3.2）。
 *
 * 实测（附录 A.3 复核）：`/networks` 返回的**已经是完整对象**——IPAM、Options、
 * Labels、Containers 都在里面，因此详情页不需要再发一次 `/networks/{id}`
 * （少一次往返，也不会出现"列表与详情不一致"的中间态）。
 *
 * 字段按可选处理：不同驱动给的字段差别很大（`host`/`none` 没有 IPAM，
 * overlay 有 `Peers`，swarm 有 `Ingress`…），缺字段一律走默认值。
 */
struct DockerNetworkDTO {
    QString id;
    QString name;
    QString driver;
    QString scope;
    QString created; /*!< RFC3339，交给 domain 解析 */
    bool internal = false;
    bool attachable = false;
    bool ingress = false;
    QList<QPair<QString, QString>> ipamConfigs;
    QList<QPair<QString, QString>> options;
    QList<QPair<QString, QString>> labels;
    QList<NetworkMember> members;

    static std::optional<DockerNetworkDTO> fromJson(const QJsonObject &object, QString *error = nullptr);
    static QList<DockerNetworkDTO> listFromJson(const QByteArray &payload, QString *error = nullptr, int *skipped = nullptr);
};

/*! DTO → domain object（方向：dto → domain）。 */
Network networkFromDto(const DockerNetworkDTO &dto);
QList<Network> networksFromDto(const QList<DockerNetworkDTO> &dtos);

} // namespace Kontainer
