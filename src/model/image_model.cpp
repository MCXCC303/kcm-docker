/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/image_model.h"

#include "model/keyed_list_model.h"

namespace Kontainer
{


ImageModel::ImageModel(QObject *parent)
    : KeyedListModel<ImageModel, Image>(parent)
{
}

int ImageModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return int(m_images.size());
}

int ImageModel::count() const
{
    return int(m_images.size());
}

QVariant ImageModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_images.size()) {
        return {};
    }

    const Image &image = m_images.at(index.row());
    switch (role) {
    case IdRole:
        return image.id;
    case ShortIdRole:
        return image.shortId();
    case TagsRole:
        return image.repoTags;
    case PrimaryTagRole:
        return image.primaryTag();
    case SizeBytesRole:
        return image.sizeBytes;
    case CreatedRole:
        return image.created;
    case InUseRole:
        return image.inUse;
    case ContainerCountRole:
        return image.containerCount;
    case DanglingRole:
        return image.isDangling();
    default:
        break;
    }
    return {};
}

QHash<int, QByteArray> ImageModel::roleNames() const
{
    return {
        {IdRole, "imageId"},
        {ShortIdRole, "shortId"},
        {TagsRole, "tags"},
        {PrimaryTagRole, "primaryTag"},
        {SizeBytesRole, "sizeBytes"},
        {CreatedRole, "created"},
        {InUseRole, "inUse"},
        {ContainerCountRole, "containerCount"},
        {DanglingRole, "dangling"},
    };
}

void ImageModel::setImages(const QList<Image> &images)
{
    /*
     * Incremental sync instead of a full reset: users saw the list jump back to the top on
     * start/stop or when returning from the detail page — the unconditional `beginResetModel()`
     * was the cause, and a reset always resets the ListView scroll position. Now only real
     * row-count/order changes touch the view; pure data changes emit `dataChanged`.
     */
    const bool touched = syncRows(
        m_images,
        images,
        [](const Image &entry) {
            return entry.id;
        },
        [](const Image &lhs, const Image &rhs) {
            return !(lhs == rhs);
        });
    if (touched) {
        Q_EMIT countChanged();
    }
}

} // namespace Kontainer
