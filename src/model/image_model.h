/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "domain/image.h"

#include "model/keyed_list_model.h"
#include <QList>

namespace Kontainer
{

/*! Image list presentation model (read-only; this phase has no write operations). */
class ImageModel : public KeyedListModel<ImageModel, Image>
{
    Q_OBJECT

    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        ShortIdRole,
        TagsRole,
        PrimaryTagRole,
        SizeBytesRole,
        CreatedRole,
        InUseRole,
        ContainerCountRole,
        DanglingRole,
    };
    Q_ENUM(Roles)

    explicit ImageModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;

    void setImages(const QList<Image> &images);
    const QList<Image> &images() const
    {
        return m_images;
    }

Q_SIGNALS:
    void countChanged();

private:
    QList<Image> m_images;
};

} // namespace Kontainer
