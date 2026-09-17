/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>

namespace Kontainer
{

/*!
 * 一条镜像拉取（ARCH_V4 §2.4）。
 *
 * 拉取是**长时间运行、可并发、可以在后台继续**的操作，因此它不适合塞进
 * 「一次操作一个结果」的通知通道：这里为每一路拉取保留一条记录，
 * 界面据此显示进度、取消某一项、以及**保留失败原因**（拉取失败不能被静默丢掉）。
 */
struct ImagePullEntry {
    /*! 归一化后的引用（`alpine:latest`）。 */
    QString reference;
    /*! pulling / succeeded / failed / cancelled。 */
    QString statusKey;
    /*! 引擎当前状态原文（"Downloading"、"Pull complete"…），按数据显示、不翻译。 */
    QString statusText;
    /*! 失败原因（引擎原文）；成功与进行中为空。 */
    QString detailText;
    /*! 0.0 ~ 1.0；未知时为 -1。 */
    double progress = -1.0;
    bool progressKnown = false;
    int completedLayers = 0;
    int totalLayers = 0;
    /*! 是否仍在进行中。 */
    bool active = false;

    bool isFinished() const
    {
        return !active;
    }

    friend bool operator==(const ImagePullEntry &lhs, const ImagePullEntry &rhs)
    {
        return lhs.reference == rhs.reference && lhs.statusKey == rhs.statusKey && lhs.statusText == rhs.statusText
            && lhs.detailText == rhs.detailText && qFuzzyCompare(lhs.progress, rhs.progress)
            && lhs.progressKnown == rhs.progressKnown && lhs.completedLayers == rhs.completedLayers
            && lhs.totalLayers == rhs.totalLayers && lhs.active == rhs.active;
    }
};

/*!
 * 拉取列表模型（ARCH_V4 §2.4）。
 *
 * 顺序由控制器决定：进行中的在前，已结束的按结束时间倒序在后。
 * 与其它列表模型一样：内容未变不发信号（进度每来一行就会更新，但进度不变时不打扰视图）。
 */
class ImagePullModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY countChanged)
    Q_PROPERTY(int finishedCount READ finishedCount NOTIFY countChanged)
    Q_PROPERTY(bool empty READ empty NOTIFY countChanged)

public:
    enum Roles {
        ReferenceRole = Qt::UserRole + 1,
        StatusKeyRole,
        StatusTextRole,
        DetailTextRole,
        ProgressRole,
        ProgressKnownRole,
        CompletedLayersRole,
        TotalLayersRole,
        ActiveRole,
    };
    Q_ENUM(Roles)

    explicit ImagePullModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    int activeCount() const;
    int finishedCount() const;
    bool empty() const;

    void setEntries(const QList<ImagePullEntry> &entries);
    const QList<ImagePullEntry> &entries() const
    {
        return m_entries;
    }
    /*! 按引用查找行号；不存在返回 -1。 */
    int rowForReference(const QString &reference) const;

Q_SIGNALS:
    void countChanged();

private:
    QList<ImagePullEntry> m_entries;
};

} // namespace Kontainer
