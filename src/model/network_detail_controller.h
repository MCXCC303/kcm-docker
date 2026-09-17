/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/docker_backend_interface.h"
#include "domain/network.h"
#include "model/detail_list_model.h"

#include <QObject>
#include <QString>

namespace Kontainer
{

/*!
 * 网络详情页的控制器（ARCH_V5_V8 §3.2）。
 *
 * 数据直接来自后端已经拿到的网络列表（`/networks` 返回的就是完整对象，§3.2 实测），
 * 因此详情页**不再单独发请求**：选中的网络变了就重新取一份快照。
 *
 * 与容器/镜像详情一样，只读信息拆成三个 `DetailListModel`：
 * 成员容器、标签、驱动选项——QML 只负责排版。
 */
class NetworkDetailController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString networkId READ networkId NOTIFY changed)
    /*! 选中的网络是否还在（列表里被删掉/刷新掉了就是 false，界面据此收起详情）。 */
    Q_PROPERTY(bool valid READ valid NOTIFY changed)

    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(QString shortId READ shortId NOTIFY changed)
    Q_PROPERTY(QString driver READ driver NOTIFY changed)
    Q_PROPERTY(QString scope READ scope NOTIFY changed)
    Q_PROPERTY(QDateTime created READ created NOTIFY changed)
    Q_PROPERTY(QString subnet READ subnet NOTIFY changed)
    Q_PROPERTY(QString gateway READ gateway NOTIFY changed)
    Q_PROPERTY(bool predefined READ predefined NOTIFY changed)
    Q_PROPERTY(bool internal READ internal NOTIFY changed)
    Q_PROPERTY(bool attachable READ attachable NOTIFY changed)
    Q_PROPERTY(bool ingress READ ingress NOTIFY changed)
    Q_PROPERTY(int memberCount READ memberCount NOTIFY changed)

    /*! 成员容器（label = 名称，value = IPv4，detail = MAC）。 */
    Q_PROPERTY(Kontainer::DetailListModel *members READ members CONSTANT)
    /*! 标签（label = key，value = value）。 */
    Q_PROPERTY(Kontainer::DetailListModel *labels READ labels CONSTANT)
    /*! 驱动选项（label = key，value = value）。 */
    Q_PROPERTY(Kontainer::DetailListModel *options READ options CONSTANT)

public:
    explicit NetworkDetailController(DockerBackendInterface *backend, QObject *parent = nullptr);

    QString networkId() const;
    bool valid() const;
    QString name() const;
    QString shortId() const;
    QString driver() const;
    QString scope() const;
    QDateTime created() const;
    QString subnet() const;
    QString gateway() const;
    bool predefined() const;
    bool internal() const;
    bool attachable() const;
    bool ingress() const;
    int memberCount() const;

    DetailListModel *members() const;
    DetailListModel *labels() const;
    DetailListModel *options() const;

    /*!
     * 选中一个网络（空 id = 什么都不选）。
     *
     * 必须是 `Q_INVOKABLE`：QML 侧从 `onCompleted` 调用它（普通成员函数在 QML 里
     * 不是函数，会抛 `TypeError: ... is not a function`——这一点由 tst_qml_load 守着）。
     */
    Q_INVOKABLE void setNetworkId(const QString &id);

Q_SIGNALS:
    /*! 选中的网络或它的内容发生变化（网络列表刷新后也会发）。 */
    void changed();

private:
    void reload();

    DockerBackendInterface *m_backend = nullptr;
    QString m_networkId;
    Network m_network;
    bool m_valid = false;

    DetailListModel *m_members = nullptr;
    DetailListModel *m_labels = nullptr;
    DetailListModel *m_options = nullptr;
};

} // namespace Kontainer
