/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/docker_backend_interface.h"
#include "domain/volume.h"
#include "model/detail_list_model.h"

#include <QObject>
#include <QString>

namespace Kontainer
{

/*!
 * 数据卷详情页的控制器（ARCH_V5_V8 §3.5）。
 *
 * 数据直接来自后端已经拿到的卷列表（`/volumes` 返回的就是完整对象），
 * 因此详情页**不再单独发请求**：选中的卷变了或列表刷新了就重新取一份快照。
 *
 * 只读信息拆成两个 `DetailListModel`：标签与驱动选项。
 */
class VolumeDetailController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString volumeName READ volumeName NOTIFY changed)
    /*! 选中的卷是否还在（被删掉/刷新掉就是 false，界面据此收起详情）。 */
    Q_PROPERTY(bool valid READ valid NOTIFY changed)

    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(QString driver READ driver NOTIFY changed)
    Q_PROPERTY(QString mountpoint READ mountpoint NOTIFY changed)
    Q_PROPERTY(QDateTime created READ created NOTIFY changed)
    Q_PROPERTY(QString scope READ scope NOTIFY changed)
    /*! 占用字节；-1 = 未知（界面显示"—"）。 */
    Q_PROPERTY(qint64 sizeBytes READ sizeBytes NOTIFY changed)
    Q_PROPERTY(bool sizeKnown READ sizeKnown NOTIFY changed)
    Q_PROPERTY(int refCount READ refCount NOTIFY changed)
    Q_PROPERTY(bool usageKnown READ usageKnown NOTIFY changed)
    Q_PROPERTY(bool inUse READ inUse NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    /*! 挂载点是否可用于"在文件管理器中打开"（空的挂载点没有可打开的位置）。 */
    Q_PROPERTY(bool mountpointUsable READ mountpointUsable NOTIFY changed)

    Q_PROPERTY(Kontainer::DetailListModel *labels READ labels CONSTANT)
    Q_PROPERTY(Kontainer::DetailListModel *options READ options CONSTANT)

public:
    explicit VolumeDetailController(DockerBackendInterface *backend, QObject *parent = nullptr);

    QString volumeName() const;
    bool valid() const;
    QString name() const;
    QString driver() const;
    QString mountpoint() const;
    QDateTime created() const;
    QString scope() const;
    qint64 sizeBytes() const;
    bool sizeKnown() const;
    int refCount() const;
    bool usageKnown() const;
    bool inUse() const;
    QString status() const;
    bool mountpointUsable() const;

    DetailListModel *labels() const;
    DetailListModel *options() const;

    /*! 选中一个数据卷（空名字 = 什么都不选）。 */
    Q_INVOKABLE void selectVolume(const QString &name);

Q_SIGNALS:
    void changed();

private:
    void reload();

    DockerBackendInterface *m_backend = nullptr;
    QString m_name;
    Volume m_volume;
    bool m_valid = false;

    DetailListModel *m_labels = nullptr;
    DetailListModel *m_options = nullptr;
};

} // namespace Kontainer
