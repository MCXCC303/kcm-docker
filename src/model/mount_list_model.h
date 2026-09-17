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
 * 挂载条目（ARCH_V4 §2.1.1）。
 *
 * 与 `DetailEntry`（label/value/detail 三行文本）的区别：挂载行需要**结构化字段**
 * ——类型徽标、读写模式、宿主路径、容器路径、命名卷名、宿主路径是否真的存在，
 * 以及「能不能打开」。把这些塞进三个字符串里会让 QML 端反向解析文本。
 */
struct MountEntry {
    /*! bind / volume / tmpfs（引擎原文，小写）。 */
    QString typeKey;
    /*! 宿主路径；tmpfs 为空。 */
    QString source;
    /*! 容器内路径。 */
    QString destination;
    /*! rw / ro。 */
    QString mode;
    /*! 命名卷名；bind / 匿名卷为空。 */
    QString volumeName;
    /*! directory / missing / notADirectory / notApplicable。 */
    QString sourceStateKey;

    bool isOpenable() const
    {
        return sourceStateKey == QLatin1String("directory");
    }

    friend bool operator==(const MountEntry &lhs, const MountEntry &rhs)
    {
        return lhs.typeKey == rhs.typeKey && lhs.source == rhs.source && lhs.destination == rhs.destination && lhs.mode == rhs.mode
            && lhs.volumeName == rhs.volumeName && lhs.sourceStateKey == rhs.sourceStateKey;
    }
};

/*!
 * 挂载列表模型（ARCH_V4 §2.1.1）。
 *
 * 只承载 presentation 数据；宿主路径探测的结果由控制器填入（模型不做 I/O）。
 * 沿用 `DetailListModel` 的约定：内容未变时不发任何信号——详情页每 30 秒复核一次，
 * 无条件重置模型会让 QML 里的行反复销毁重建（ARCH_V3 附录 A.1g 的段错误诱因）。
 */
class MountListModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool empty READ empty NOTIFY countChanged)
    /*! 打不开的挂载数量（没有宿主路径或路径不存在），供页面给出提示。 */
    Q_PROPERTY(int blockedCount READ blockedCount NOTIFY countChanged)

public:
    enum Roles {
        TypeKeyRole = Qt::UserRole + 1,
        SourceRole,
        DestinationRole,
        ModeRole,
        VolumeNameRole,
        SourceStateKeyRole,
        OpenableRole,
    };
    Q_ENUM(Roles)

    explicit MountListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    bool empty() const;
    int blockedCount() const;

    void setMounts(const QList<MountEntry> &mounts);
    const QList<MountEntry> &mounts() const
    {
        return m_mounts;
    }

Q_SIGNALS:
    void countChanged();

private:
    QList<MountEntry> m_mounts;
};

} // namespace Kontainer
