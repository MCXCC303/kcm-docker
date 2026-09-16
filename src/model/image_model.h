/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "domain/image.h"

#include <QAbstractListModel>
#include <QList>

namespace Kontainer
{

/*! 镜像列表的 presentation model（只读；一期不含任何写操作）。 */
class ImageModel : public QAbstractListModel
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
