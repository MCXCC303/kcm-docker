/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/port_mapping_model.h"

namespace Kontainer
{

QString PortMappingEntry::containerChipText() const
{
    return QStringLiteral("%1/%2").arg(containerPort).arg(protocol.isEmpty() ? QStringLiteral("tcp") : protocol);
}

QString PortMappingEntry::hostChipText() const
{
    if (!isPublished()) {
        return {};
    }
    return QStringLiteral("%1:%2").arg(hostIp.isEmpty() ? QStringLiteral("0.0.0.0") : hostIp).arg(hostPort);
}

PortMappingModel::PortMappingModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int PortMappingModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_mappings.size());
}

QVariant PortMappingModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_mappings.size()) {
        return {};
    }
    const PortMappingEntry &mapping = m_mappings.at(index.row());
    switch (role) {
    case ContainerPortRole:
        return mapping.containerPort;
    case ProtocolRole:
        return mapping.protocol;
    case HostIpRole:
        return mapping.hostIp;
    case HostPortRole:
        return mapping.hostPort;
    case PublishedRole:
        return mapping.isPublished();
    case ContainerChipTextRole:
        return mapping.containerChipText();
    case HostChipTextRole:
        return mapping.hostChipText();
    default:
        return {};
    }
}

QHash<int, QByteArray> PortMappingModel::roleNames() const
{
    return {
        {ContainerPortRole, QByteArrayLiteral("containerPort")},
        {ProtocolRole, QByteArrayLiteral("protocol")},
        {HostIpRole, QByteArrayLiteral("hostIp")},
        {HostPortRole, QByteArrayLiteral("hostPort")},
        {PublishedRole, QByteArrayLiteral("published")},
        {ContainerChipTextRole, QByteArrayLiteral("containerChipText")},
        {HostChipTextRole, QByteArrayLiteral("hostChipText")},
    };
}

int PortMappingModel::count() const
{
    return int(m_mappings.size());
}

bool PortMappingModel::empty() const
{
    return m_mappings.isEmpty();
}

void PortMappingModel::setMappings(const QList<PortMappingEntry> &mappings)
{
    if (m_mappings == mappings) {
        return;
    }
    beginResetModel();
    m_mappings = mappings;
    endResetModel();
    Q_EMIT countChanged();
}

} // namespace Kontainer
