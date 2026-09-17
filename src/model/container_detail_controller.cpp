/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/container_detail_controller.h"

#include "logging.h"
#include "model/docker_error_text.h"
#include "model/state_text.h"
#include "refresh_policy.h"

#include <KLocalizedString>

#include <QTimer>

namespace Kontainer
{

using Section = DockerBackendInterface::Section;

namespace
{

QString portLabel(const Port &port)
{
    return QStringLiteral("%1/%2").arg(port.privatePort).arg(port.type.isEmpty() ? QStringLiteral("tcp") : port.type);
}

QString portValue(const Port &port)
{
    if (!port.isPublished()) {
        return {};
    }
    const QString host = port.ip.isEmpty() || port.ip == QLatin1String("0.0.0.0") ? QString() : port.ip + QLatin1Char(':');
    return host + QString::number(port.publicPort);
}

} // namespace

ContainerDetailController::ContainerDetailController(DockerBackendInterface *backend, QObject *parent)
    : QObject(parent)
    , m_backend(backend)
    , m_reinspectTimer(new QTimer(this))
    , m_metrics(new MetricsModel(this))
    , m_ports(new DetailListModel(this))
    , m_networks(new DetailListModel(this))
    , m_mounts(new DetailListModel(this))
    , m_labels(new DetailListModel(this))
    , m_environment(new DetailListModel(this))
{
    Q_ASSERT(m_backend);
    m_metrics->setBackend(m_backend);

    // 静态信息低频复核（§28）：动态数据由 metrics 负责
    m_reinspectTimer->setInterval(
        int(std::chrono::duration_cast<std::chrono::milliseconds>(RefreshPolicy::kDetailRefreshInterval).count()));
    connect(m_reinspectTimer, &QTimer::timeout, this, [this] {
        if (m_started && !m_containerId.isEmpty()) {
            m_backend->inspectContainer(m_containerId);
        }
    });

    connect(m_backend, &DockerBackendInterface::containerDetailUpdated, this, &ContainerDetailController::onDetailUpdated);
    connect(m_backend, &DockerBackendInterface::containerStatsUpdated, this, &ContainerDetailController::onStatsUpdated);
    connect(m_backend, &DockerBackendInterface::sectionFailed, this, &ContainerDetailController::onSectionFailed);
}

ContainerDetailController::~ContainerDetailController() = default;

void ContainerDetailController::setContainerId(const QString &id)
{
    if (m_containerId == id) {
        return;
    }
    m_containerId = id;
    m_detail = ContainerDetail();
    setLoadState(QStringLiteral("idle"));
    Q_EMIT containerIdChanged();
    Q_EMIT changed();
}

QString ContainerDetailController::shortId() const
{
    return m_detail.id.left(12);
}

QString ContainerDetailController::stateKey() const
{
    return containerStateKey(m_detail.state);
}

QString ContainerDetailController::stateText() const
{
    return containerStateText(m_detail.state);
}

QString ContainerDetailController::healthKey() const
{
    return healthStateKey(m_detail.health);
}

QString ContainerDetailController::healthText() const
{
    return healthStateText(m_detail.health);
}

void ContainerDetailController::setLoadState(const QString &stateKey, const QString &errorText)
{
    if (m_loadStateKey == stateKey && m_errorText == errorText) {
        return;
    }
    m_loadStateKey = stateKey;
    m_errorText = errorText;
    Q_EMIT stateChanged();
}

void ContainerDetailController::start()
{
    m_started = true;
    if (m_containerId.isEmpty()) {
        setLoadState(QStringLiteral("error"), i18n("No container selected."));
        return;
    }
    setLoadState(QStringLiteral("loading"));
    m_backend->inspectContainer(m_containerId);
    m_reinspectTimer->start();
}

void ContainerDetailController::stop()
{
    m_started = false;
    m_reinspectTimer->stop();
    // 离开页面：停止 stats 采样并释放历史（§27）
    m_metrics->stop();
}

void ContainerDetailController::refresh()
{
    if (m_containerId.isEmpty()) {
        return;
    }
    setLoadState(QStringLiteral("loading"));
    m_backend->inspectContainer(m_containerId);
}

void ContainerDetailController::reload()
{
    if (m_containerId.isEmpty()) {
        return;
    }
    m_backend->inspectContainer(m_containerId);
}

void ContainerDetailController::onDetailUpdated()
{
    const ContainerDetail detail = m_backend->containerDetail();
    if (detail.id != m_containerId) {
        return; // 不是当前页面关心的容器
    }

    m_detail = detail;
    rebuildLists();
    setLoadState(QStringLiteral("ready"));

    // 只有运行中/暂停的容器才有资源数据（§17）；已停止容器不轮询 stats
    if (running()) {
        m_metrics->start(m_containerId);
    } else {
        m_metrics->stop();
    }
    Q_EMIT changed();
}

void ContainerDetailController::onStatsUpdated()
{
    if (!m_started || m_containerId.isEmpty()) {
        return;
    }
    const ContainerStats stats = m_backend->containerStats();
    if (stats.containerId != m_containerId) {
        return;
    }
    m_metrics->addSample(stats);
}

void ContainerDetailController::onSectionFailed(Section section, const DockerError &error)
{
    if (!m_started) {
        return;
    }
    if (section == Section::ContainerDetail) {
        // 详情失败不影响列表页（§31）
        setLoadState(QStringLiteral("error"), dockerErrorText(error));
        return;
    }
    if (section == Section::Stats) {
        qCDebug(kontainerBackend) << "stats sampling failed:" << error.detail();
        m_metrics->noteFailure();
    }
}

void ContainerDetailController::rebuildLists()
{
    QList<DetailEntry> ports;
    ports.reserve(m_detail.ports.size());
    for (const Port &port : m_detail.ports) {
        ports.append({portLabel(port), portValue(port), QString(), port.type.isEmpty() ? QStringLiteral("tcp") : port.type});
    }
    m_ports->setEntries(ports);

    QList<DetailEntry> networks;
    networks.reserve(m_detail.networks.size());
    for (const ContainerNetwork &network : m_detail.networks) {
        // label=网络名 value=IPv4 detail=MAC，其余（IPv6/网关）作为附加条目展开
        networks.append({network.name, network.ipAddress, network.macAddress, QStringLiteral("network")});
        if (!network.ipv6Address.isEmpty()) {
            networks.append({i18n("IPv6"), network.ipv6Address, network.name, QStringLiteral("network-ipv6")});
        }
        if (!network.gateway.isEmpty()) {
            networks.append({i18n("Gateway"), network.gateway, network.name, QStringLiteral("network-gateway")});
        }
    }
    m_networks->setEntries(networks);

    QList<DetailEntry> mounts;
    mounts.reserve(m_detail.mounts.size());
    for (const ContainerMount &mount : m_detail.mounts) {
        QString detail = mount.type;
        if (!mount.mode.isEmpty()) {
            detail += QLatin1Char(' ') + mount.mode;
        }
        if (mount.readOnly) {
            detail += QStringLiteral(" (ro)");
        }
        mounts.append({mount.destination, mount.source, detail, mount.type});
    }
    m_mounts->setEntries(mounts);

    QList<DetailEntry> labels;
    labels.reserve(m_detail.labels.size());
    for (const auto &[key, value] : m_detail.labels) {
        labels.append({key, value, QString(), QStringLiteral("label")});
    }
    m_labels->setEntries(labels);

    QList<DetailEntry> environment;
    environment.reserve(m_detail.environment.size());
    for (const QString &entry : m_detail.environment) {
        const int equals = entry.indexOf(QLatin1Char('='));
        if (equals > 0) {
            environment.append({entry.left(equals), entry.mid(equals + 1), QString(), QStringLiteral("env")});
        } else {
            environment.append({entry, QString(), QString(), QStringLiteral("env")});
        }
    }
    m_environment->setEntries(environment);
}

} // namespace Kontainer
