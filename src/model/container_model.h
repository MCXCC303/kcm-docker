/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "domain/container.h"

#include "model/keyed_list_model.h"
#include <QList>

namespace Kontainer
{

/*!
 * Presentation model for the container list (ARCH_V1 §13).
 *
 * Role names are fixed; do not change them for ad-hoc QML needs.
 * Only converts domain data into QML-consumable data, never accesses Docker.
 */
class ContainerModel : public KeyedListModel<ContainerModel, Container>
{
    Q_OBJECT

    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        ShortIdRole,
        ImageRole,
        ImageIdRole,
        StateKeyRole,
        StateTextRole,
        StatusRole,
        CreatedRole,
        HealthKeyRole,
        HealthTextRole,
        PortsSummaryRole,
        PortCountRole,
    };
    Q_ENUM(Roles)

    explicit ContainerModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;

    void setContainers(const QList<Container> &containers);
    const QList<Container> &containers() const
    {
        return m_containers;
    }

Q_SIGNALS:
    void countChanged();

private:
    QList<Container> m_containers;
};

} // namespace Kontainer
