/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/image_model.h"

namespace Kontainer
{

ImageModel::ImageModel(QObject *parent)
    : QAbstractListModel(parent)
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
    if (m_images == images) {
        return; // 数据没有变化：不发信号
    }

    beginResetModel();
    m_images = images;
    endResetModel();
    Q_EMIT countChanged();
}

} // namespace Kontainer
