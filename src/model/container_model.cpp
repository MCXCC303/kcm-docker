/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/container_model.h"

#include "model/keyed_list_model.h"

#include "model/state_text.h"

#include <QStringList>

namespace Kontainer
{


namespace
{

/*!
 * Port summary: published ports render as "8080→80/tcp", unpublished as "80/tcp".
 * This is the port data itself, not UI copy.
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
    : KeyedListModel<ContainerModel, Container>(parent)
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
    /*
     * Incremental sync instead of a full reset: users reported "the list jumps back to the top after
     * start/stop, or when returning from the detail page" -- the cause was an unconditional
     * `beginResetModel()`, and a model reset always scrolls a ListView back to the top. Now the view is
     * only adjusted when the row count/order really changed; pure data changes just emit `dataChanged`.
     */
    const bool touched = syncRows(
        m_containers,
        containers,
        [](const Container &entry) {
            return entry.id;
        },
        [](const Container &lhs, const Container &rhs) {
            return !(lhs == rhs);
        });
    if (touched) {
        Q_EMIT countChanged();
    }
}

} // namespace Kontainer
