/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/host_port_model.h"

namespace Kontainer
{

HostPortModel::HostPortModel(QObject *parent)
    : KeyedListModel<HostPortModel, HostPortEntry>(parent)
{
}

QString HostPortModel::keyFor(const HostPortEntry &entry)
{
    return QStringLiteral("%1|%2|%3|%4|%5")
        .arg(entry.hostPort)
        .arg(entry.containerId, entry.protocol)
        .arg(entry.containerPort)
        .arg(entry.hostPortEnd);
}

int HostPortModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_entries.size());
}

QVariant HostPortModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) {
        return {};
    }
    const HostPortEntry &entry = m_entries.at(index.row());
    switch (role) {
    case PortTextRole:
        return entry.portText();
    case HostPortRole:
        return int(entry.hostPort);
    case AddressTextRole:
        return entry.displayAddress();
    case ContainerPortRole:
        return int(entry.containerPort);
    case ProtocolRole:
        return entry.protocol;
    case StateKeyRole:
        return entry.stateKey;
    case ContainerNameRole:
        return entry.containerName;
    case ContainerIdRole:
        return entry.containerId;
    case ContainerImageRole:
        return entry.containerImage;
    case ActionableRole:
        // 只有真的占着端口的容器才谈得上"停止"；声明未生效的只给跳转
        return entry.stateKey == QLatin1String("inUse");
    default:
        break;
    }
    return {};
}

QHash<int, QByteArray> HostPortModel::roleNames() const
{
    return {
        {PortTextRole, QByteArrayLiteral("portText")},
        {HostPortRole, QByteArrayLiteral("hostPort")},
        {AddressTextRole, QByteArrayLiteral("addressText")},
        {ContainerPortRole, QByteArrayLiteral("containerPort")},
        {ProtocolRole, QByteArrayLiteral("protocol")},
        {StateKeyRole, QByteArrayLiteral("stateKey")},
        {ContainerNameRole, QByteArrayLiteral("containerName")},
        {ContainerIdRole, QByteArrayLiteral("containerId")},
        {ContainerImageRole, QByteArrayLiteral("containerImage")},
        {ActionableRole, QByteArrayLiteral("actionable")},
    };
}

int HostPortModel::count() const
{
    return int(m_entries.size());
}

bool HostPortModel::empty() const
{
    return m_entries.isEmpty();
}

const QList<HostPortEntry> &HostPortModel::entries() const
{
    return m_entries;
}

void HostPortModel::setEntries(const QList<HostPortEntry> &entries)
{
    const bool changed = syncRows(
        m_entries, entries, [](const HostPortEntry &entry) { return keyFor(entry); },
        [](const HostPortEntry &lhs, const HostPortEntry &rhs) { return !(lhs == rhs); });
    if (changed) {
        Q_EMIT countChanged();
    }
}

} // namespace Kontainer
