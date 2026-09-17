/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/image_pull_model.h"

namespace Kontainer
{

ImagePullModel::ImagePullModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int ImagePullModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_entries.size());
}

QVariant ImagePullModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) {
        return {};
    }
    const ImagePullEntry &entry = m_entries.at(index.row());
    switch (role) {
    case ReferenceRole:
        return entry.reference;
    case ErrorKindKeyRole:
        return entry.errorKindKey;
    case StatusKeyRole:
        return entry.statusKey;
    case StatusTextRole:
        return entry.statusText;
    case DetailTextRole:
        return entry.detailText;
    case ProgressRole:
        return entry.progressKnown ? entry.progress : -1.0;
    case ProgressKnownRole:
        return entry.progressKnown;
    case CompletedLayersRole:
        return entry.completedLayers;
    case TotalLayersRole:
        return entry.totalLayers;
    case ActiveRole:
        return entry.active;
    default:
        return {};
    }
}

QHash<int, QByteArray> ImagePullModel::roleNames() const
{
    return {
        {ReferenceRole, QByteArrayLiteral("reference")},
        {StatusKeyRole, QByteArrayLiteral("statusKey")},
        {ErrorKindKeyRole, QByteArrayLiteral("errorKindKey")},
        {StatusTextRole, QByteArrayLiteral("statusText")},
        {DetailTextRole, QByteArrayLiteral("detailText")},
        {ProgressRole, QByteArrayLiteral("progress")},
        {ProgressKnownRole, QByteArrayLiteral("progressKnown")},
        {CompletedLayersRole, QByteArrayLiteral("completedLayers")},
        {TotalLayersRole, QByteArrayLiteral("totalLayers")},
        {ActiveRole, QByteArrayLiteral("active")},
    };
}

int ImagePullModel::count() const
{
    return int(m_entries.size());
}

int ImagePullModel::activeCount() const
{
    int active = 0;
    for (const ImagePullEntry &entry : m_entries) {
        if (entry.active) {
            ++active;
        }
    }
    return active;
}

int ImagePullModel::finishedCount() const
{
    return count() - activeCount();
}

bool ImagePullModel::empty() const
{
    return m_entries.isEmpty();
}

int ImagePullModel::rowForReference(const QString &reference) const
{
    for (int row = 0; row < m_entries.size(); ++row) {
        if (m_entries.at(row).reference == reference) {
            return row;
        }
    }
    return -1;
}

void ImagePullModel::setEntries(const QList<ImagePullEntry> &entries)
{
    if (m_entries == entries) {
        return;
    }
    beginResetModel();
    m_entries = entries;
    endResetModel();
    Q_EMIT countChanged();
}

} // namespace Kontainer
