/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "domain/image_build.h"

#include <QAbstractListModel>
#include <QList>

namespace Kontainer
{

/*!
 * 构建列表里的一条（ARCH_V5_V8 §5.3）。
 *
 * 与拉取同一个模式：构建也是"长时间、可并发、可取消、失败原因不能丢"的任务，
 * 因此不复用「一次操作一个结果」的通知通道。
 */
struct ImageBuildEntry {
    QString id;
    /*! 上下文目录（再次构建时用）。 */
    QString contextDirectory;
    /*! 标签（展示用；可能多个）。 */
    QStringList tags;
    /*! building / succeeded / failed / cancelled。 */
    QString statusKey = QStringLiteral("building");
    /*! 引擎状态原文（`Step 3/7 : RUN make`），按数据显示、不翻译。 */
    QString statusText;
    /*! 由 `statusText` 解析出来的步骤信息。 */
    int stepIndex = 0;
    int totalSteps = 0;
    QString stepCommand;
    /*! 失败原因（含失败步骤的描述，引擎原文拼装）。 */
    QString detailText;
    QString errorKindKey;
    /*! 0.0 ~ 1.0（按步骤数推算）；未知时 -1。 */
    double progress = -1.0;
    bool progressKnown = false;
    /*! 成功后的镜像 id。 */
    QString imageId;
    bool active = true;

    friend bool operator==(const ImageBuildEntry &lhs, const ImageBuildEntry &rhs)
    {
        return lhs.id == rhs.id && lhs.statusKey == rhs.statusKey && lhs.statusText == rhs.statusText
            && lhs.stepIndex == rhs.stepIndex && lhs.totalSteps == rhs.totalSteps && lhs.stepCommand == rhs.stepCommand
            && lhs.detailText == rhs.detailText && lhs.errorKindKey == rhs.errorKindKey
            && qFuzzyCompare(lhs.progress, rhs.progress) && lhs.progressKnown == rhs.progressKnown
            && lhs.imageId == rhs.imageId && lhs.active == rhs.active;
    }
};

/*!
 * 构建列表模型。
 *
 * 与拉取列表一样：内容没变就不发信号（进度每来一行都会更新，但重复行不该打扰视图）。
 */
class ImageBuildModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY countChanged)
    Q_PROPERTY(int finishedCount READ finishedCount NOTIFY countChanged)
    Q_PROPERTY(bool empty READ empty NOTIFY countChanged)

public:
    enum Roles {
        BuildIdRole = Qt::UserRole + 1,
        TagsRole,
        StatusKeyRole,
        StatusTextRole,
        StepIndexRole,
        TotalStepsRole,
        StepCommandRole,
        DetailTextRole,
        ProgressRole,
        ProgressKnownRole,
        ImageIdRole,
        ActiveRole,
        ErrorKindKeyRole,
    };
    Q_ENUM(Roles)

    explicit ImageBuildModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    int activeCount() const;
    int finishedCount() const;
    bool empty() const;

    void setEntries(const QList<ImageBuildEntry> &entries);
    const QList<ImageBuildEntry> &entries() const
    {
        return m_entries;
    }
    /*! 按构建 id 找行号；不存在返回 -1（Q_INVOKABLE：QML 只能调用暴露出来的函数）。 */
    Q_INVOKABLE int rowForBuildId(const QString &buildId) const;

Q_SIGNALS:
    void countChanged();

private:
    QList<ImageBuildEntry> m_entries;
};

} // namespace Kontainer
