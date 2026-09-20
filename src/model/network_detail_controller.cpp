/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/network_detail_controller.h"

namespace Kontainer
{

NetworkDetailController::NetworkDetailController(DockerBackendInterface *backend, QObject *parent)
    : QObject(parent)
    , m_backend(backend)
    , m_members(new DetailListModel(this))
    , m_labels(new DetailListModel(this))
    , m_options(new DetailListModel(this))
{
    Q_ASSERT(m_backend);
    // re-snapshot after a list refresh: the detail page shows the network as it is right now
    connect(m_backend, &DockerBackendInterface::networksUpdated, this, &NetworkDetailController::reload);
}

QString NetworkDetailController::networkId() const
{
    return m_networkId;
}

bool NetworkDetailController::valid() const
{
    return m_valid;
}

QString NetworkDetailController::name() const
{
    return m_network.name;
}

QString NetworkDetailController::shortId() const
{
    return m_network.shortId();
}

QString NetworkDetailController::driver() const
{
    return m_network.driver;
}

QString NetworkDetailController::scope() const
{
    return m_network.scope;
}

QDateTime NetworkDetailController::created() const
{
    return m_network.created;
}

QString NetworkDetailController::subnet() const
{
    return m_network.subnetText();
}

QString NetworkDetailController::gateway() const
{
    return m_network.primaryGateway();
}

bool NetworkDetailController::predefined() const
{
    return m_network.isPredefined();
}

bool NetworkDetailController::internal() const
{
    return m_network.internal;
}

bool NetworkDetailController::attachable() const
{
    return m_network.attachable;
}

bool NetworkDetailController::ingress() const
{
    return m_network.ingress;
}

int NetworkDetailController::memberCount() const
{
    return m_network.memberCount();
}

DetailListModel *NetworkDetailController::members() const
{
    return m_members;
}

DetailListModel *NetworkDetailController::labels() const
{
    return m_labels;
}

DetailListModel *NetworkDetailController::options() const
{
    return m_options;
}

void NetworkDetailController::setNetworkId(const QString &id)
{
    if (m_networkId == id) {
        reload();
        return;
    }
    m_networkId = id;
    reload();
    Q_EMIT changed();
}

void NetworkDetailController::reload()
{
    Network found;
    bool valid = false;
    if (!m_networkId.isEmpty()) {
        const QList<Network> networks = m_backend->networks();
        for (const Network &network : networks) {
            if (network.id == m_networkId || (m_networkId.size() >= 12 && network.id.startsWith(m_networkId))) {
                found = network;
                valid = true;
                break;
            }
        }
    }

    const bool changedState = m_valid != valid || !(m_network == found);
    m_network = found;
    m_valid = valid;

    QList<DetailEntry> memberEntries;
    memberEntries.reserve(found.members.size());
    for (const NetworkMember &member : found.members) {
        DetailEntry entry;
        entry.label = member.name.isEmpty() ? member.containerId.left(12) : member.name;
        entry.value = member.ipv4Address;
        entry.detail = member.macAddress;
        // container id in entryKey: QML uses it to jump to the container detail (the row shows no id)
        entry.entryKey = member.containerId;
        entry.target = member.containerId;
        // state icon: same look as the image "used by" list (users asked for consistency)
        for (const Container &container : m_backend->containers()) {
            if (container.id == member.containerId) {
                entry.stateKey = container.stateKey();
                break;
            }
        }
        memberEntries.append(entry);
    }
    m_members->setEntries(memberEntries);

    QList<DetailEntry> labelEntries;
    labelEntries.reserve(found.labels.size());
    for (const auto &label : found.labels) {
        labelEntries.append({label.first, label.second, QString(), QString(), QString(), QString()});
    }
    m_labels->setEntries(labelEntries);

    QList<DetailEntry> optionEntries;
    optionEntries.reserve(found.options.size());
    for (const auto &option : found.options) {
        optionEntries.append({option.first, option.second, QString(), QString(), QString(), QString()});
    }
    m_options->setEntries(optionEntries);

    if (changedState) {
        Q_EMIT changed();
    }
}

} // namespace Kontainer
