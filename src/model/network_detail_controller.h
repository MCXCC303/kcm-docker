/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
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
 * Controller for the network detail page (ARCH_V5_V8 §3.2).
 *
 * Data comes straight from the network list the backend already has (`/networks` returns full
 * objects, §3.2 measured), so the detail page **sends no request of its own**: it only takes a
 * fresh snapshot when the selection changes.
 *
 * As with container/image detail, read-only information is split into three `DetailListModel`s —
 * member containers, labels, driver options — and QML only lays them out.
 */
class NetworkDetailController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString networkId READ networkId NOTIFY changed)
    /*! Whether the selected network still exists (false once a refresh drops it); the UI hides the detail. */
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

    /*! Member containers (label = name, value = IPv4, detail = MAC). */
    Q_PROPERTY(Kontainer::DetailListModel *members READ members CONSTANT)
    /*! Labels (label = key, value = value). */
    Q_PROPERTY(Kontainer::DetailListModel *labels READ labels CONSTANT)
    /*! Driver options (label = key, value = value). */
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
     * Select a network (empty id selects nothing).
     *
     * Must be `Q_INVOKABLE`: QML calls it from `onCompleted`, and a plain member function is not a
     * function there — it throws `TypeError: ... is not a function`, which tst_qml_load guards.
     */
    Q_INVOKABLE void setNetworkId(const QString &id);

Q_SIGNALS:
    /*! The selected network or its content changed (also emitted after a network list refresh). */
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
