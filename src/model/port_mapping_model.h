/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>

namespace Kontainer
{

/*!
 * One port mapping (ARCH_V4 §2.1.2).
 *
 * Unlike phase 2, which joined ports into label/value text rows: the topology view needs structured
 * fields — container port, protocol, host IP, host port — otherwise the UI could only parse strings
 * back.
 */
struct PortMappingEntry {
    quint16 containerPort = 0;
    /*! tcp / udp / sctp. */
    QString protocol;
    /*! Host binding address (0.0.0.0 / 127.0.0.1 / :: …). */
    QString hostIp;
    /*! Host port; 0 means not published. */
    quint16 hostPort = 0;
    /*!
     * This entry stands for both the IPv4 and the IPv6 wildcard binding.
     *
     * For a mapping without a host address, Docker creates both `0.0.0.0:<port>` and `[::]:<port>`;
     * drawing them as two nodes would suggest two mappings, so the controller merges them into one
     * entry with this flag and the UI shows "IPv4 + IPv6" as a double ring.
     */
    bool dualStack = false;

    bool isPublished() const
    {
        return hostPort != 0;
    }
    /*! Container-side chip text, e.g. `80/tcp`. */
    QString containerChipText() const;
    /*! Host-side chip text, e.g. `0.0.0.0:8080`; empty when unpublished. */
    QString hostChipText() const;

    friend bool operator==(const PortMappingEntry &lhs, const PortMappingEntry &rhs)
    {
        return lhs.containerPort == rhs.containerPort && lhs.protocol == rhs.protocol && lhs.hostIp == rhs.hostIp && lhs.hostPort == rhs.hostPort
            && lhs.dualStack == rhs.dualStack;
    }
};

/*!
 * Port mapping model (ARCH_V4 §2.1.2).
 *
 * The controller keeps two instances: published (drawn into the topology) and unpublished (EXPOSEd
 * but never mapped). The split is obvious: a line needs endpoints at both ends, and an unpublished
 * port has no host endpoint — forcing it into the topology would leave dangling lines or holes.
 *
 * Like `DetailListModel`: unchanged content emits nothing.
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
