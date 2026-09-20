/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "domain/network.h"

#include "model/keyed_list_model.h"
#include <QList>

namespace Kontainer
{

/*!
 * Network list model (ARCH_V5_V8 §3.2).
 *
 * Like the container/image lists: emit nothing when unchanged (a background refresh must not
 * rebuild delegates); sorting belongs to the proxy model, the model keeps the daemon's order.
 *
 * Everything the list page needs (name, driver, scope, subnet, member count, predefined) is a role,
 * and so are the members/labels/options the detail page needs — `/networks` already returns full
 * objects (§3.2, measured), so the detail page reuses the same data and sends no extra request.
 */
class NetworkModel : public KeyedListModel<NetworkModel, Network>
{
    Q_OBJECT

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool empty READ empty NOTIFY countChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        IdRole,
        ShortIdRole,
        DriverRole,
        ScopeRole,
        CreatedRole,
        SubnetRole,
        GatewayRole,
        /*! Whether this is a daemon-predefined network (bridge/host/none); the UI hides delete. */
        PredefinedRole,
        InternalRole,
        AttachableRole,
        IngressRole,
        MemberCountRole,
        /*! Member containers (`QList<NetworkMember>`), used by the detail page. */
        MembersRole,
        /*! Labels and driver options (`QList<QPair<QString,QString>>`). */
        LabelsRole,
        OptionsRole,
    };
    Q_ENUM(Roles)

    explicit NetworkModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    bool empty() const;
    const QList<Network> &networks() const;
    /*! Unchanged content leaves the model untouched. */
    void setNetworks(const QList<Network> &networks);
    /*! Clear (on failure, stale data must not keep passing as current). */
    void clear();

    /*!
     * All network names in current order.
     *
     * The UI sometimes needs the networks **without a delegate** (e.g. the connect dialog counts
     * how many are still connectable), and QML cannot read a model by role.
     */
    Q_INVOKABLE QStringList names() const;

    /*!
     * Plain-data summary of all networks: `[{id, name, driver, predefined}]`.
     *
     * For UIs that need the list **without a delegate** (e.g. the connect-network dialog): QML
     * cannot read a model by role, and the dialog has its own instance tree inside a popup, so a
     * plain array is far more reliable than the model object.
     */
    Q_INVOKABLE QVariantList summaries() const;

    /*! Row for an Id, -1 if not found (used for detail-page navigation). */
    Q_INVOKABLE int rowForId(const QString &id) const;
    /*!
     * Id for a name (empty if not found).
     *
     * Container detail only exposes network names (inspect's `NetworkSettings.Networks` is keyed by
     * name) while connect/disconnect take a network Id — this is the single conversion point.
     */
    Q_INVOKABLE QString idForName(const QString &name) const;

Q_SIGNALS:
    void countChanged();

private:
    QList<Network> m_networks;
};

} // namespace Kontainer
