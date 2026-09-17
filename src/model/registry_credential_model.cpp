/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/registry_credential_model.h"

namespace Kontainer
{

RegistryCredentialModel::RegistryCredentialModel(CredentialStore *store, QObject *parent)
    : QAbstractListModel(parent)
    , m_store(store)
{
    if (m_store) {
        connect(m_store, &CredentialStore::changed, this, &RegistryCredentialModel::reload);
    }
}

int RegistryCredentialModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return int(m_entries.size());
}

QVariant RegistryCredentialModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) {
        return {};
    }
    const Entry &entry = m_entries.at(index.row());
    switch (role) {
    case ServerAddressRole:
        return entry.serverAddress;
    case AuthKindRole:
        return entry.authKind;
    case UsernameRole:
        return entry.username;
    default:
        break;
    }
    return {};
}

QHash<int, QByteArray> RegistryCredentialModel::roleNames() const
{
    return {
        {ServerAddressRole, QByteArrayLiteral("serverAddress")},
        {AuthKindRole, QByteArrayLiteral("authKind")},
        {UsernameRole, QByteArrayLiteral("username")},
    };
}

int RegistryCredentialModel::count() const
{
    return int(m_entries.size());
}

bool RegistryCredentialModel::empty() const
{
    return m_entries.isEmpty();
}

bool RegistryCredentialModel::contains(const QString &serverAddress) const
{
    return rowForAddress(serverAddress) >= 0;
}

int RegistryCredentialModel::rowForAddress(const QString &serverAddress) const
{
    const QString normalized = RegistryAuth::normalizeServerAddress(serverAddress);
    for (int row = 0; row < m_entries.size(); ++row) {
        if (m_entries.at(row).serverAddress == normalized) {
            return row;
        }
    }
    return -1;
}

void RegistryCredentialModel::reload()
{
    QList<Entry> refreshed;
    if (m_store) {
        const QStringList addresses = m_store->serverAddresses();
        for (const QString &address : addresses) {
            const RegistryCredential credential = m_store->credential(address);
            if (credential.isEmpty()) {
                continue; // 坏条目不出现在列表里（控制器另有"有问题"的提示）
            }
            Entry entry;
            entry.serverAddress = address;
            // 只复制"可以给界面看"的字段：密码/令牌绝不进模型
            entry.authKind = credential.usesIdentityToken() ? QStringLiteral("token") : QStringLiteral("password");
            entry.username = credential.username;
            refreshed.append(entry);
        }
    }

    // 内容未变就不动模型：否则每次刷新都会重建 delegate（列表抖动）
    if (refreshed.size() == m_entries.size()) {
        bool identical = true;
        for (int row = 0; row < refreshed.size(); ++row) {
            if (refreshed.at(row).serverAddress != m_entries.at(row).serverAddress
                || refreshed.at(row).authKind != m_entries.at(row).authKind
                || refreshed.at(row).username != m_entries.at(row).username) {
                identical = false;
                break;
            }
        }
        if (identical) {
            return;
        }
    }

    beginResetModel();
    m_entries = refreshed;
    endResetModel();
    Q_EMIT countChanged();
}

} // namespace Kontainer
