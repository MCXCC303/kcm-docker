/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QDateTime>
#include <QList>
#include <QMetaType>
#include <QPair>
#include <QString>
#include <QStringList>

namespace Kontainer
{

/*!
 * A container attached to a network (the `Containers` map of `GET /networks`).
 *
 * A **read-only snapshot**: the daemon owns the container-to-network relationship and the UI draws
 * no inferences from it.
 */
struct NetworkMember {
    QString containerId;
    QString name;
    /*! Address without the subnet prefix (the daemon sends `172.18.0.2/16`). */
    QString ipv4Address;
    QString ipv6Address;
    QString macAddress;

    bool operator==(const NetworkMember &other) const
    {
        return containerId == other.containerId && name == other.name && ipv4Address == other.ipv4Address
            && ipv6Address == other.ipv6Address && macAddress == other.macAddress;
    }
};

/*!
 * Docker network (ARCH_V5_V8 §3.2).
 *
 * Phase 6 only **creates bridge networks** (§3.3), but list and detail must show other drivers
 * (overlay / macvlan / host / null…) as they are: recognizing and showing them needs no extra
 * privilege, creating one does.
 */
struct Network {
    QString id;
    QString name;
    /*! bridge / host / null / overlay / macvlan … (`null` is the none network). */
    QString driver;
    /*! local / swarm / global. */
    QString scope;
    QDateTime created;
    bool internal = false;
    bool attachable = false;
    bool ingress = false;
    /*! IPAM config (subnet + gateway); usually empty for host/none networks. */
    QList<QPair<QString, QString>> ipamConfigs;
    /*! Driver options (key/value pairs, sorted by key). */
    QList<QPair<QString, QString>> options;
    QList<QPair<QString, QString>> labels;
    QList<NetworkMember> members;

    bool isValid() const
    {
        return !id.isEmpty() && !name.isEmpty();
    }
    /*! 12-character short ID. */
    QString shortId() const;
    /*!
     * Whether this is a daemon-predefined network (`bridge` / `host` / `none`).
     *
     * Decided by name, exactly as the daemon does: removing one answers
     * `403 bridge is a pre-defined network and cannot be removed` (appendix A.3). The UI therefore
     * **hides the delete entry**; if one still slips through (an older engine, say), the 403 is
     * mapped to a "the user can fix this" message (§3.2).
     */
    bool isPredefined() const;
    /*! All subnets joined with `, ` (empty when there are none). */
    QString subnetText() const;
    /*! Primary IPAM gateway (empty when there is none). */
    QString primaryGateway() const;
    /*! Number of containers attached to this network. */
    int memberCount() const;
    /*! Containers beyond the first member (shown as `+N` in the list). */
    int extraMemberCount() const;

    bool operator==(const Network &other) const;
};

/*!
 * Payload submitted when creating a network (ARCH_V5_V8 §3.3).
 *
 * Phase 6 creates **bridge** networks only (the scope the user confirmed): `driver` is kept so the
 * request mirrors what is really sent to the daemon, but the UI only ever fills in `bridge`.
 */
struct NetworkCreateRequest {
    QString name;
    /*! Always `bridge`; other drivers are only recognized this round (§3.3, deviating on purpose). */
    QString driver = QStringLiteral("bridge");
    /*! Optional subnet (e.g. `172.20.0.0/16`) and gateway; empty lets the daemon assign them. */
    QString subnet;
    QString gateway;
    bool internal = false;
    bool attachable = false;
    QList<QPair<QString, QString>> labels;

    /*! JSON sent to the daemon (keys match the Docker API; empty fields are omitted). */
    QByteArray toJson() const;
};

/*!
 * Network name validation rules (shared by the UI and the controller).
 *
 * An empty string means pass; otherwise a **stable error key** is returned (the text lives in QML),
 * as with the rest of the project (C++ never assembles user-visible text).
 */
QString validateNetworkName(const QString &name);
/*! Subnet / gateway format validation (an empty string counts as "not filled in"). */
QString validateSubnet(const QString &subnet);
QString validateGateway(const QString &gateway, const QString &subnet);

} // namespace Kontainer

Q_DECLARE_METATYPE(Kontainer::Network)
Q_DECLARE_METATYPE(QList<Kontainer::Network>)
