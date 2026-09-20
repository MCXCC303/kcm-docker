/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
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
     * Incremental sync, not a full reset: `beginResetModel()` ran unconditionally and a
     * reset always scrolls a ListView to the top, which users saw as the list jumping back
     * after start/stop or returning from detail; now only row/order changes move the view.
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
    // Take the incremental path (row-by-row removal): clearing skips the full reset that scrolls to top
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
        // Only **definitely** unused volumes are prunable: never guess on unknown refs (prune deletes data)
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
