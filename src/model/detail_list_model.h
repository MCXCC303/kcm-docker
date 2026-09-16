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
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countChanged)
    Q_PROPERTY(bool empty READ empty NOTIFY countChanged)
    /*!
     * 只暴露前 N 条（0 = 不限制）。
     *
     * 用于「镜像层默认只显示前 5 层、展开后显示全部」（ARCH_V3 §2.3）：
     * QAbstractListModel 无法在 QML 里切片，而视图也不应该创建看不见的 delegate，
     * 因此把可见条数交给 model 层控制。
     */
    Q_PROPERTY(int limit READ limit WRITE setLimit NOTIFY limitChanged)

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
    /*! 全部条目数（不受 limit 影响）。 */
    int totalCount() const;
    bool empty() const;

    int limit() const
    {
        return m_limit;
    }
    void setLimit(int limit);

    void setEntries(const QList<DetailEntry> &entries);

Q_SIGNALS:
    void countChanged();
    void limitChanged();

private:
    QList<DetailEntry> m_entries;
    int m_limit = 0; /*!< 0 = 不限制 */
};

} // namespace Kontainer
