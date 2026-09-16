/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "model/detail_entry.h"

#include <QAbstractListModel>
#include <QList>

namespace Kontainer
{

/*!
 * 通用「只读条目列表」模型（端口 / 网络 / 挂载 / 标签 / 环境变量 / 镜像层 / 关联容器）。
 *
 * 只承载 presentation 数据，不访问 Docker，也不解析 JSON。
 */
class DetailListModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool empty READ empty NOTIFY countChanged)

public:
    enum Roles {
        LabelRole = Qt::UserRole + 1,
        ValueRole,
        DetailRole,
        EntryKeyRole,
    };
    Q_ENUM(Roles)

    explicit DetailListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    bool empty() const;

    void setEntries(const QList<DetailEntry> &entries);

Q_SIGNALS:
    void countChanged();

private:
    QList<DetailEntry> m_entries;
};

} // namespace Kontainer
