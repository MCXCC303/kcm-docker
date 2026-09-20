/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/port_mapping_group_model.h"

#include <algorithm>

namespace Kontainer
{

QString PortMappingGroup::containerChipText() const
{
    return QStringLiteral("%1/%2").arg(containerPort).arg(protocol.isEmpty() ? QStringLiteral("tcp") : protocol);
}

QStringList PortMappingGroup::hostChipTexts() const
{
    QStringList texts;
    texts.reserve(bindings.size());
    for (const PortMappingEntry &binding : bindings) {
        texts.append(binding.hostChipText());
    }
    return texts;
}

PortMappingGroupModel::PortMappingGroupModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int PortMappingGroupModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return int(m_groups.size());
}

QVariant PortMappingGroupModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_groups.size()) {
        return {};
    }
    const PortMappingGroup &group = m_groups.at(index.row());
    switch (role) {
    case ContainerPortRole:
        return group.containerPort;
    case ProtocolRole:
        return group.protocol;
    case ContainerChipTextRole:
        return group.containerChipText();
    case HostChipTextsRole:
        return group.hostChipTexts();
    case DualStackFlagsRole: {
        QVariantList flags;
        flags.reserve(group.bindings.size());
        for (const PortMappingEntry &binding : group.bindings) {
            flags.append(binding.dualStack);
        }
        return flags;
    }
    case BindingCountRole:
        return int(group.bindings.size());
    default:
        break;
    }
    return {};
}

QHash<int, QByteArray> PortMappingGroupModel::roleNames() const
{
    return {
        {ContainerPortRole, QByteArrayLiteral("containerPort")},
        {ProtocolRole, QByteArrayLiteral("protocol")},
        {ContainerChipTextRole, QByteArrayLiteral("containerChipText")},
        {HostChipTextsRole, QByteArrayLiteral("hostChipTexts")},
        {DualStackFlagsRole, QByteArrayLiteral("dualStackFlags")},
        {BindingCountRole, QByteArrayLiteral("bindingCount")},
    };
}

int PortMappingGroupModel::count() const
{
    return int(m_groups.size());
}

bool PortMappingGroupModel::empty() const
{
    return m_groups.isEmpty();
}

int PortMappingGroupModel::bindingCount() const
{
    int total = 0;
    for (const PortMappingGroup &group : m_groups) {
        total += int(group.bindings.size());
    }
    return total;
}

const QList<PortMappingGroup> &PortMappingGroupModel::groups() const
{
    return m_groups;
}

void PortMappingGroupModel::setEntries(const QList<PortMappingEntry> &entries)
{
    QList<PortMappingGroup> grouped;
    for (const PortMappingEntry &entry : entries) {
        if (!entry.isPublished()) {
            continue; // unpublished ports have no host endpoint, so they stay out of the topology (page groups them)
        }
        const QString protocol = entry.protocol.isEmpty() ? QStringLiteral("tcp") : entry.protocol;
        auto it = std::find_if(grouped.begin(), grouped.end(), [&entry, &protocol](const PortMappingGroup &group) {
            return group.containerPort == entry.containerPort && group.protocol == protocol;
        });
        if (it == grouped.end()) {
            PortMappingGroup group;
            group.containerPort = entry.containerPort;
            group.protocol = protocol;
            group.bindings.append(entry);
            grouped.append(group);
            continue;
        }
        it->bindings.append(entry);
    }

    // sort: groups by container port/protocol, bindings inside a group by host port/host address.
    // deterministic order, so rows never jump on refresh (as with the other models)
    for (PortMappingGroup &group : grouped) {
        std::sort(group.bindings.begin(), group.bindings.end(), [](const PortMappingEntry &lhs, const PortMappingEntry &rhs) {
            if (lhs.hostPort != rhs.hostPort) {
                return lhs.hostPort < rhs.hostPort;
            }
            return lhs.hostIp < rhs.hostIp;
        });
    }
    std::sort(grouped.begin(), grouped.end(), [](const PortMappingGroup &lhs, const PortMappingGroup &rhs) {
        if (lhs.containerPort != rhs.containerPort) {
            return lhs.containerPort < rhs.containerPort;
        }
        return lhs.protocol < rhs.protocol;
    });

    if (grouped == m_groups) {
        return; // unchanged: leave the model alone (avoids rebuilding delegates on refresh)
    }

    beginResetModel();
    m_groups = grouped;
    endResetModel();
    Q_EMIT countChanged();
}

} // namespace Kontainer
