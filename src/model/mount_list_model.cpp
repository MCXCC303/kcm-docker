/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/mount_list_model.h"

namespace Kontainer
{

MountListModel::MountListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int MountListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_mounts.size());
}

QVariant MountListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_mounts.size()) {
        return {};
    }
    const MountEntry &mount = m_mounts.at(index.row());
    switch (role) {
    case TypeKeyRole:
        return mount.typeKey;
    case SourceRole:
        return mount.source;
    case DestinationRole:
        return mount.destination;
    case ModeRole:
        return mount.mode;
    case VolumeNameRole:
        return mount.volumeName;
    case SourceStateKeyRole:
        return mount.sourceStateKey;
    case OpenableRole:
        return mount.isOpenable();
    default:
        return {};
    }
}

QHash<int, QByteArray> MountListModel::roleNames() const
{
    return {
        {TypeKeyRole, QByteArrayLiteral("typeKey")},
        {SourceRole, QByteArrayLiteral("source")},
        {DestinationRole, QByteArrayLiteral("destination")},
        {ModeRole, QByteArrayLiteral("mode")},
        {VolumeNameRole, QByteArrayLiteral("volumeName")},
        {SourceStateKeyRole, QByteArrayLiteral("sourceStateKey")},
        {OpenableRole, QByteArrayLiteral("openable")},
    };
}

int MountListModel::count() const
{
    return int(m_mounts.size());
}

bool MountListModel::empty() const
{
    return m_mounts.isEmpty();
}

int MountListModel::blockedCount() const
{
    int blocked = 0;
    for (const MountEntry &mount : m_mounts) {
        if (!mount.isOpenable() && mount.sourceStateKey != QLatin1String("notApplicable")) {
            ++blocked;
        }
    }
    return blocked;
}

void MountListModel::setMounts(const QList<MountEntry> &mounts)
{
    // 内容没变就不发信号（ARCH_V2 §32/§34，ARCH_V3 附录 A.1e）
    if (m_mounts == mounts) {
        return;
    }
    beginResetModel();
    m_mounts = mounts;
    endResetModel();
    Q_EMIT countChanged();
}

} // namespace Kontainer
