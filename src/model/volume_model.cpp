/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/volume_model.h"

#include "model/keyed_list_model.h"

#include <QVariantMap>

namespace Kontainer
{


VolumeModel::VolumeModel(QObject *parent)
    : KeyedListModel<VolumeModel, Volume>(parent)
{
}

int VolumeModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return int(m_volumes.size());
}

QVariant VolumeModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_volumes.size()) {
        return {};
    }
    const Volume &volume = m_volumes.at(index.row());
    switch (role) {
    case NameRole:
        return volume.name;
    case DriverRole:
        return volume.driver;
    case MountpointRole:
        return volume.mountpoint;
    case CreatedRole:
        return volume.createdAt;
    case ScopeRole:
        return volume.scope;
    case SizeBytesRole:
        return volume.sizeBytes;
    case SizeKnownRole:
        return volume.sizeKnown();
    case RefCountRole:
        return volume.refCount;
    case InUseRole:
        return volume.isInUse();
    case UsageKnownRole:
        return volume.usageKnown();
    case LabelsRole:
        return QVariant::fromValue(volume.labels);
    case OptionsRole:
        return QVariant::fromValue(volume.options);
    case StatusRole:
        return volume.status;
    default:
        break;
    }
    return {};
}

QHash<int, QByteArray> VolumeModel::roleNames() const
{
    return {
        {NameRole, QByteArrayLiteral("name")},
        {DriverRole, QByteArrayLiteral("driver")},
        {MountpointRole, QByteArrayLiteral("mountpoint")},
        {CreatedRole, QByteArrayLiteral("created")},
        {ScopeRole, QByteArrayLiteral("scope")},
        {SizeBytesRole, QByteArrayLiteral("sizeBytes")},
        {SizeKnownRole, QByteArrayLiteral("sizeKnown")},
        {RefCountRole, QByteArrayLiteral("refCount")},
        {InUseRole, QByteArrayLiteral("inUse")},
        {UsageKnownRole, QByteArrayLiteral("usageKnown")},
        {LabelsRole, QByteArrayLiteral("labels")},
        {OptionsRole, QByteArrayLiteral("options")},
        {StatusRole, QByteArrayLiteral("status")},
    };
}

int VolumeModel::count() const
{
    return int(m_volumes.size());
}

bool VolumeModel::empty() const
{
    return m_volumes.isEmpty();
}

const QList<Volume> &VolumeModel::volumes() const
{
    return m_volumes;
}

void VolumeModel::setVolumes(const QList<Volume> &volumes)
{
    /*
     * 增量同步（而不是整表重置）：用户实测"点启动/停止、或从详情页返回后，列表被拉回最上方"——
     * 根因是原来无条件 `beginResetModel()`，而模型重置必然让 ListView 跳回顶部。
     * 现在只有行数/顺序真的变了才调整视图位置，纯数据变化只发 `dataChanged`。
     */
    const bool touched = syncRows(
        m_volumes,
        volumes,
        [](const Volume &entry) {
            return entry.name;
        },
        [](const Volume &lhs, const Volume &rhs) {
            return !(lhs == rhs);
        });
    if (touched) {
        Q_EMIT countChanged();
    }
}

void VolumeModel::clear()
{
    if (m_volumes.isEmpty()) {
        return;
    }
    // 走增量路径（逐行删除）：清空时也不整表重置，视图位置因此不会被拉回顶部
    const bool touched = syncRows(m_volumes,
                                  QList<Volume>(),
                                  [](const Volume &entry) {
                                      return entry.name;
                                  },
                                  [](const Volume &lhs, const Volume &rhs) {
                                      return !(lhs == rhs);
                                  });
    if (touched) {
        Q_EMIT countChanged();
    }
}

int VolumeModel::rowForName(const QString &name) const
{
    for (int row = 0; row < m_volumes.size(); ++row) {
        if (m_volumes.at(row).name == name) {
            return row;
        }
    }
    return -1;
}

QStringList VolumeModel::names() const
{
    QStringList result;
    result.reserve(m_volumes.size());
    for (const Volume &volume : m_volumes) {
        result.append(volume.name);
    }
    return result;
}

QStringList VolumeModel::unusedNames() const
{
    QStringList result;
    for (const Volume &volume : m_volumes) {
        // 只有**确定**没被使用才算"可清理"：引用数未知时不能猜（prune 会真的删数据）
        if (volume.usageKnown() && !volume.isInUse()) {
            result.append(volume.name);
        }
    }
    return result;
}

qint64 VolumeModel::knownUnusedSize() const
{
    qint64 total = 0;
    for (const Volume &volume : m_volumes) {
        if (volume.usageKnown() && !volume.isInUse() && volume.sizeKnown()) {
            total += volume.sizeBytes;
        }
    }
    return total;
}

int VolumeModel::unknownUnusedSizeCount() const
{
    int count = 0;
    for (const Volume &volume : m_volumes) {
        if (volume.usageKnown() && !volume.isInUse() && !volume.sizeKnown()) {
            ++count;
        }
    }
    return count;
}

QVariantList VolumeModel::summaries() const
{
    QVariantList result;
    result.reserve(m_volumes.size());
    for (const Volume &volume : m_volumes) {
        result.append(QVariantMap {
            {QStringLiteral("name"), volume.name},
            {QStringLiteral("driver"), volume.driver},
            {QStringLiteral("mountpoint"), volume.mountpoint},
            {QStringLiteral("inUse"), volume.isInUse()},
        });
    }
    return result;
}

} // namespace Kontainer
