/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/container_model.h"

#include "model/state_text.h"

#include <QStringList>

namespace Kontainer
{

namespace
{

/*!
 * 端口概要：已发布端口显示为 "8080→80/tcp"，未发布显示为 "80/tcp"。
 * 这是端口数据本身，不是界面文案。
 */
QString portsSummary(const Container &container)
{
    QStringList parts;
    parts.reserve(container.ports.size());
    for (const Port &port : container.ports) {
        if (port.isPublished()) {
            parts.append(QStringLiteral("%1→%2/%3").arg(port.publicPort).arg(port.privatePort).arg(port.type));
        } else {
            parts.append(QStringLiteral("%1/%2").arg(port.privatePort).arg(port.type));
        }
    }
    return parts.join(QStringLiteral(", "));
}

} // namespace

ContainerModel::ContainerModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int ContainerModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return int(m_containers.size());
}

int ContainerModel::count() const
{
    return int(m_containers.size());
}

QVariant ContainerModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_containers.size()) {
        return {};
    }

    const Container &container = m_containers.at(index.row());
    switch (role) {
    case IdRole:
        return container.id;
    case NameRole:
        return container.name;
    case ShortIdRole:
        return container.shortId();
    case ImageRole:
        return container.image;
    case ImageIdRole:
        return container.imageId;
    case StateKeyRole:
        return container.stateKey();
    case StateTextRole:
        return containerStateText(container.state);
    case StatusRole:
        return container.status;
    case CreatedRole:
        return container.created;
    case HealthKeyRole:
        return container.healthKey();
    case HealthTextRole:
        return healthStateText(container.health);
    case PortsSummaryRole:
        return portsSummary(container);
    case PortCountRole:
        return int(container.ports.size());
    default:
        break;
    }
    return {};
}

QHash<int, QByteArray> ContainerModel::roleNames() const
{
    return {
        {IdRole, "containerId"},
        {NameRole, "name"},
        {ShortIdRole, "shortId"},
        {ImageRole, "image"},
        {ImageIdRole, "imageId"},
        {StateKeyRole, "stateKey"},
        {StateTextRole, "stateText"},
        {StatusRole, "status"},
        {CreatedRole, "created"},
        {HealthKeyRole, "healthKey"},
        {HealthTextRole, "healthText"},
        {PortsSummaryRole, "portsSummary"},
        {PortCountRole, "portCount"},
    };
}

void ContainerModel::setContainers(const QList<Container> &containers)
{
    if (m_containers == containers) {
        return; // 数据没有变化：不发信号，列表与滚动位置保持不动
    }

    beginResetModel();
    m_containers = containers;
    endResetModel();
    Q_EMIT countChanged();
}

} // namespace Kontainer
