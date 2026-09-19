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
 * 一条端口映射（ARCH_V4 §2.1.2）。
 *
 * 与二期把端口拼成「label / value 两行文本」的区别：拓扑图需要结构化字段
 * ——容器端口、协议、宿主 IP、宿主端口——否则界面只能反向解析字符串。
 */
struct PortMappingEntry {
    quint16 containerPort = 0;
    /*! tcp / udp / sctp。 */
    QString protocol;
    /*! 宿主绑定地址（0.0.0.0 / 127.0.0.1 / :: …）。 */
    QString hostIp;
    /*! 宿主端口；0 表示没有发布。 */
    quint16 hostPort = 0;
    /*!
     * 这一条同时代表 IPv4 与 IPv6 的通配绑定。
     *
     * Docker 对"没有指定宿主地址"的映射会同时建 `0.0.0.0:<port>` 与 `[::]:<port>` 两条；
     * 拓扑图把它们画成两个节点只会让人以为映射了两份，因此控制器合并成一条并打上这个标记，
     * 界面用"双环"表示"IPv4 + IPv6"。
     */
    bool dualStack = false;

    bool isPublished() const
    {
        return hostPort != 0;
    }
    /*! 容器侧芯片文本，例如 `80/tcp`。 */
    QString containerChipText() const;
    /*! 宿主侧芯片文本，例如 `0.0.0.0:8080`；未发布时为空。 */
    QString hostChipText() const;

    friend bool operator==(const PortMappingEntry &lhs, const PortMappingEntry &rhs)
    {
        return lhs.containerPort == rhs.containerPort && lhs.protocol == rhs.protocol && lhs.hostIp == rhs.hostIp && lhs.hostPort == rhs.hostPort
            && lhs.dualStack == rhs.dualStack;
    }
};

/*!
 * 端口映射模型（ARCH_V4 §2.1.2）。
 *
 * 控制器维护两个实例：已发布的（画进拓扑）与未发布的（EXPOSE 了但没映射）。
 * 分开的原因很直接：连线的两端都需要端点，未发布的端口没有宿主端点，
 * 硬塞进拓扑只会留下悬空的线或者空洞。
 *
 * 与 `DetailListModel` 一样：内容未变不发信号。
 */
class PortMappingModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool empty READ empty NOTIFY countChanged)

public:
    enum Roles {
        ContainerPortRole = Qt::UserRole + 1,
        ProtocolRole,
        HostIpRole,
        HostPortRole,
        PublishedRole,
        ContainerChipTextRole,
        HostChipTextRole,
    };
    Q_ENUM(Roles)

    explicit PortMappingModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    bool empty() const;

    void setMappings(const QList<PortMappingEntry> &mappings);
    const QList<PortMappingEntry> &mappings() const
    {
        return m_mappings;
    }

Q_SIGNALS:
    void countChanged();

private:
    QList<PortMappingEntry> m_mappings;
};

} // namespace Kontainer
