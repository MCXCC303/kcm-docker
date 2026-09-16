/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "domain/container.h"

#include <QAbstractListModel>
#include <QList>

namespace Kontainer
{

/*!
 * 容器列表的 presentation model（ARCH_V1 §13）。
 *
 * role 名称固定，不为了 QML 的临时需求随意变更。
 * 只做“域数据 → QML 可消费数据”的转换，不做任何 Docker 访问。
 */
class ContainerModel : public QAbstractListModel
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
