/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/image_build_model.h"

namespace Kontainer
{

ImageBuildModel::ImageBuildModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int ImageBuildModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return int(m_entries.size());
}

QVariant ImageBuildModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) {
        return {};
    }
    const ImageBuildEntry &entry = m_entries.at(index.row());
    switch (role) {
    case BuildIdRole:
        return entry.id;
    case TagsRole:
        return entry.tags;
    case StatusKeyRole:
        return entry.statusKey;
    case StatusTextRole:
        return entry.statusText;
    case StepIndexRole:
        return entry.stepIndex;
    case TotalStepsRole:
        return entry.totalSteps;
    case StepCommandRole:
        return entry.stepCommand;
    case DetailTextRole:
        return entry.detailText;
    case ProgressRole:
        return entry.progress;
    case ProgressKnownRole:
        return entry.progressKnown;
    case ImageIdRole:
        return entry.imageId;
    case ActiveRole:
        return entry.active;
    case ErrorKindKeyRole:
        return entry.errorKindKey;
    default:
        return {};
    }
}

QHash<int, QByteArray> ImageBuildModel::roleNames() const
{
    static const QHash<int, QByteArray> roles = {
        {BuildIdRole, QByteArrayLiteral("buildId")},
        {TagsRole, QByteArrayLiteral("tags")},
        {StatusKeyRole, QByteArrayLiteral("statusKey")},
        {StatusTextRole, QByteArrayLiteral("statusText")},
        {StepIndexRole, QByteArrayLiteral("stepIndex")},
        {TotalStepsRole, QByteArrayLiteral("totalSteps")},
        {StepCommandRole, QByteArrayLiteral("stepCommand")},
        {DetailTextRole, QByteArrayLiteral("detailText")},
        {ProgressRole, QByteArrayLiteral("progress")},
        {ProgressKnownRole, QByteArrayLiteral("progressKnown")},
        {ImageIdRole, QByteArrayLiteral("imageId")},
        {ActiveRole, QByteArrayLiteral("active")},
        {ErrorKindKeyRole, QByteArrayLiteral("errorKindKey")},
    };
    return roles;
}

int ImageBuildModel::count() const
{
    return int(m_entries.size());
}

int ImageBuildModel::activeCount() const
{
    int active = 0;
    for (const ImageBuildEntry &entry : m_entries) {
        if (entry.active) {
            ++active;
        }
    }
    return active;
}

int ImageBuildModel::finishedCount() const
{
    return count() - activeCount();
}

bool ImageBuildModel::empty() const
{
    return m_entries.isEmpty();
}

int ImageBuildModel::rowForBuildId(const QString &buildId) const
{
    for (int row = 0; row < m_entries.size(); ++row) {
        if (m_entries.at(row).id == buildId) {
            return row;
        }
    }
    return -1;
}

void ImageBuildModel::setEntries(const QList<ImageBuildEntry> &entries)
{
    if (entries == m_entries) {
        return; // unchanged: emit nothing (progress lines arrive densely; duplicates must not disturb the view)
    }

    if (entries.size() == m_entries.size()) {
        // same count: compare row by row and emit dataChanged only for rows that really changed
        bool sameShape = true;
        for (int row = 0; row < entries.size(); ++row) {
            if (entries.at(row).id != m_entries.at(row).id) {
                sameShape = false;
                break;
            }
        }
        if (sameShape) {
            m_entries = entries;
            for (int row = 0; row < entries.size(); ++row) {
                Q_EMIT dataChanged(index(row, 0), index(row, 0));
            }
            Q_EMIT countChanged();
            return;
        }
    }

    beginResetModel();
    m_entries = entries;
    endResetModel();
    Q_EMIT countChanged();
}

} // namespace Kontainer
