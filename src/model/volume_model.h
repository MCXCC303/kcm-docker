/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "domain/volume.h"

#include <QAbstractListModel>
#include <QList>

namespace Kontainer
{

/*!
 * 数据卷列表模型（ARCH_V5_V8 §3.5）。
 *
 * 与容器/镜像/网络列表同一套约定：内容未变不发信号；排序与过滤交给代理模型；
 * 列表页要的字段（名称、驱动、挂载点、大小、引用数、是否在用）与详情页要的
 * （标签、选项、状态）都在 role 里——`/volumes` 返回的就是完整对象。
 */
class VolumeModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool empty READ empty NOTIFY countChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        DriverRole,
        MountpointRole,
        CreatedRole,
        ScopeRole,
        /*! 占用字节；-1 = 未知（界面显示"—"而不是 0）。 */
        SizeBytesRole,
        SizeKnownRole,
        /*! 使用它的容器数；-1 = 未知。 */
        RefCountRole,
        /*! 是否**确定**有容器在用（未知时为 false）。 */
        InUseRole,
        UsageKnownRole,
        LabelsRole,
        OptionsRole,
        StatusRole,
    };
    Q_ENUM(Roles)

    explicit VolumeModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    bool empty() const;
    const QList<Volume> &volumes() const;
    /*! 内容未变则完全不动模型。 */
    void setVolumes(const QList<Volume> &volumes);
    void clear();

    /*! 按名字找行；找不到返回 -1。 */
    Q_INVOKABLE int rowForName(const QString &name) const;
    /*! 全部名字（界面在"没有 delegate"时也要列表，例如删除确认前的核对）。 */
    Q_INVOKABLE QStringList names() const;
    /*! 未使用的卷（prune 前的预览要用，且**不**依赖引擎的 prune 结果）。 */
    Q_INVOKABLE QStringList unusedNames() const;
    /*! 纯数据摘要 `[{name, driver, mountpoint, inUse}]`（面板/对话框用）。 */
    Q_INVOKABLE QVariantList summaries() const;
    /*! 未使用卷里**已知**大小之和（清理预览用；未知的不计入）。 */
    Q_INVOKABLE qint64 knownUnusedSize() const;
    /*! 未使用卷里大小未知的个数（预览要如实说明"还有 N 个大小未知"）。 */
    Q_INVOKABLE int unknownUnusedSizeCount() const;

Q_SIGNALS:
    void countChanged();

private:
    QList<Volume> m_volumes;
};

} // namespace Kontainer
