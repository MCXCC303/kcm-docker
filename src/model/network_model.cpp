/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/network_model.h"

namespace Kontainer
{

NetworkModel::NetworkModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int NetworkModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return int(m_networks.size());
}

QVariant NetworkModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_networks.size()) {
        return {};
    }
    const Network &network = m_networks.at(index.row());
    switch (role) {
    case NameRole:
        return network.name;
    case IdRole:
        return network.id;
    case ShortIdRole:
        return network.shortId();
    case DriverRole:
        return network.driver.isEmpty() ? QStringLiteral("bridge") : network.driver;
    case ScopeRole:
        return network.scope;
    case CreatedRole:
        return network.created;
    case SubnetRole:
        return network.subnetText();
    case GatewayRole:
        return network.primaryGateway();
    case PredefinedRole:
        return network.isPredefined();
    case InternalRole:
        return network.internal;
    case AttachableRole:
        return network.attachable;
    case IngressRole:
        return network.ingress;
    case MemberCountRole:
        return network.memberCount();
    case MembersRole:
        return QVariant::fromValue(network.members);
    case LabelsRole:
        return QVariant::fromValue(network.labels);
    case OptionsRole:
        return QVariant::fromValue(network.options);
    default:
        break;
    }
    return {};
}

QHash<int, QByteArray> NetworkModel::roleNames() const
{
    return {
        {NameRole, QByteArrayLiteral("name")},
        {IdRole, QByteArrayLiteral("id")},
        {ShortIdRole, QByteArrayLiteral("shortId")},
        {DriverRole, QByteArrayLiteral("driver")},
        {ScopeRole, QByteArrayLiteral("scope")},
        {CreatedRole, QByteArrayLiteral("created")},
        {SubnetRole, QByteArrayLiteral("subnet")},
        {GatewayRole, QByteArrayLiteral("gateway")},
        {PredefinedRole, QByteArrayLiteral("predefined")},
        {InternalRole, QByteArrayLiteral("internal")},
        {AttachableRole, QByteArrayLiteral("attachable")},
        {IngressRole, QByteArrayLiteral("ingress")},
        {MemberCountRole, QByteArrayLiteral("memberCount")},
        {MembersRole, QByteArrayLiteral("members")},
        {LabelsRole, QByteArrayLiteral("labels")},
        {OptionsRole, QByteArrayLiteral("options")},
    };
}

int NetworkModel::count() const
{
    return int(m_networks.size());
}

bool NetworkModel::empty() const
{
    return m_networks.isEmpty();
}

const QList<Network> &NetworkModel::networks() const
{
    return m_networks;
}

void NetworkModel::setNetworks(const QList<Network> &networks)
{
    if (networks == m_networks) {
        return; // 内容未变：不动模型（后台刷新不重建 delegate）
    }
    beginResetModel();
    m_networks = networks;
    endResetModel();
    Q_EMIT countChanged();
}

void NetworkModel::clear()
{
    if (m_networks.isEmpty()) {
        return;
    }
    beginResetModel();
    m_networks.clear();
    endResetModel();
    Q_EMIT countChanged();
}

int NetworkModel::rowForId(const QString &id) const
{
    for (int row = 0; row < m_networks.size(); ++row) {
        const Network &network = m_networks.at(row);
        if (network.id == id || (id.size() >= 12 && network.id.startsWith(id))) {
            return row;
        }
    }
    return -1;
}

} // namespace Kontainer
