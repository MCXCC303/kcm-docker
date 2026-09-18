/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/network_model.h"

#include "model/keyed_list_model.h"

#include <QVariantMap>

namespace Kontainer
{


NetworkModel::NetworkModel(QObject *parent)
    : KeyedListModel<NetworkModel, Network>(parent)
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
    /*
     * 增量同步（而不是整表重置）：用户实测"点启动/停止、或从详情页返回后，列表被拉回最上方"——
     * 根因是原来无条件 `beginResetModel()`，而模型重置必然让 ListView 跳回顶部。
     * 现在只有行数/顺序真的变了才调整视图位置，纯数据变化只发 `dataChanged`。
     */
    const bool touched = syncRows(
        m_networks,
        networks,
        [](const Network &entry) {
            return entry.id;
        },
        [](const Network &lhs, const Network &rhs) {
            return !(lhs == rhs);
        });
    if (touched) {
        Q_EMIT countChanged();
    }
}

void NetworkModel::clear()
{
    if (m_networks.isEmpty()) {
        return;
    }
    // 走增量路径（逐行删除）：清空时也不整表重置，视图位置因此不会被拉回顶部
    const bool touched = syncRows(m_networks,
                                  QList<Network>(),
                                  [](const Network &entry) {
                                      return entry.id;
                                  },
                                  [](const Network &lhs, const Network &rhs) {
                                      return !(lhs == rhs);
                                  });
    if (touched) {
        Q_EMIT countChanged();
    }
}

QVariantList NetworkModel::summaries() const
{
    QVariantList result;
    result.reserve(m_networks.size());
    for (const Network &network : m_networks) {
        result.append(QVariantMap {
            {QStringLiteral("id"), network.id},
            {QStringLiteral("name"), network.name},
            {QStringLiteral("driver"), network.driver.isEmpty() ? QStringLiteral("bridge") : network.driver},
            {QStringLiteral("predefined"), network.isPredefined()},
        });
    }
    return result;
}

QStringList NetworkModel::names() const
{
    QStringList result;
    result.reserve(m_networks.size());
    for (const Network &network : m_networks) {
        result.append(network.name);
    }
    return result;
}

QString NetworkModel::idForName(const QString &name) const
{
    for (const Network &network : m_networks) {
        if (network.name == name) {
            return network.id;
        }
    }
    return {};
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
