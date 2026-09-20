/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
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

ContainerDetailController::ContainerDetailController(DockerBackendInterface *backend, HostPathService *hostPaths, QObject *parent)
    : QObject(parent)
    , m_backend(backend)
    , m_hostPaths(hostPaths)
    , m_reinspectTimer(new QTimer(this))
    , m_metrics(new MetricsModel(this))
    , m_logs(new ContainerLogController(backend, this))
    , m_publishedPorts(new PortMappingModel(this))
    , m_portGroups(new PortMappingGroupModel(this))
    , m_unpublishedPorts(new PortMappingModel(this))
    , m_networks(new DetailListModel(this))
    , m_mounts(new MountListModel(this))
    , m_labels(new DetailListModel(this))
    , m_environment(new DetailListModel(this))
{
    Q_ASSERT(m_backend);
    m_metrics->setBackend(m_backend);

    if (m_hostPaths) {
        connect(m_hostPaths, &HostPathService::openFinished, this, [this](HostPathError error, const QString &detail) {
            if (error == HostPathError::None) {
                setMountActionError(QString());
                return;
            }
            switch (error) {
            case HostPathError::Missing:
                setMountActionError(i18n("The host directory of this mount does not exist."));
                break;
            case HostPathError::NotADirectory:
                setMountActionError(i18n("This mount has no host directory to open."));
                break;
            case HostPathError::LaunchFailed:
                setMountActionError(i18n("Could not open the file manager: %1", detail));
                break;
            case HostPathError::None:
                break;
            }
        });
    }

    // Low-frequency re-check of static info (§28): dynamic data belongs to metrics
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
    if (m_containerId != id) {
        // Switching containers: the old one may still hold a log stream
        m_logs->disconnect();
    }
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
    // Leaving the page: stop stats sampling and drop history (§27)
    m_metrics->stop();
    // Logs are a long-lived stream: leaving the page must disconnect, or it stays open (§3.1.4)
    m_logs->disconnect();
}

QStringList ContainerDetailController::connectedNetworkNames() const
{
    QStringList names;
    names.reserve(m_detail.networks.size());
    for (const ContainerNetwork &network : m_detail.networks) {
        names.append(network.name);
    }
    return names;
}

void ContainerDetailController::startLogs()
{
    if (m_containerId.isEmpty()) {
        return;
    }
    m_logs->connectTo(m_containerId, m_detail.tty);
}

void ContainerDetailController::stopLogs()
{
    m_logs->disconnect();
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
        return; // Not the container this page cares about
    }

    m_detail = detail;
    rebuildLists();
    setLoadState(QStringLiteral("ready"));

    // Only running/paused containers have resource data (§17); stopped ones are not polled for stats
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
        // A detail failure must not affect the list page (§31)
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
    rebuildPorts();

    QList<DetailEntry> networks;
    networks.reserve(m_detail.networks.size());
    for (const ContainerNetwork &network : m_detail.networks) {
        // label=network name, value=IPv4, detail=MAC; IPv6/gateway become extra entries
        networks.append({network.name, network.ipAddress, network.macAddress, QStringLiteral("network"), QString(), QString()});
        if (!network.ipv6Address.isEmpty()) {
            networks.append({i18n("IPv6"), network.ipv6Address, network.name, QStringLiteral("network-ipv6"), QString(), QString()});
        }
        if (!network.gateway.isEmpty()) {
            networks.append({i18n("Gateway"), network.gateway, network.name, QStringLiteral("network-gateway"), QString(), QString()});
        }
    }
    m_networks->setEntries(networks);

    m_mounts->setMounts(mountEntries());

    QList<DetailEntry> labels;
    labels.reserve(m_detail.labels.size());
    for (const auto &[key, value] : m_detail.labels) {
        labels.append({key, value, QString(), QStringLiteral("label"), QString(), QString()});
    }
    m_labels->setEntries(labels);

    QList<DetailEntry> environment;
    environment.reserve(m_detail.environment.size());
    for (const QString &entry : m_detail.environment) {
        const int equals = entry.indexOf(QLatin1Char('='));
        if (equals > 0) {
            environment.append({entry.left(equals), entry.mid(equals + 1), QString(), QStringLiteral("env"), QString(), QString()});
        } else {
            environment.append({entry, QString(), QString(), QStringLiteral("env"), QString(), QString()});
        }
    }
    m_environment->setEntries(environment);
}

namespace
{
/*! Wildcard address (IPv4 0.0.0.0 / IPv6 :: or [::]). */
bool isWildcardAddress(const QString &hostIp)
{
    return hostIp.isEmpty() || hostIp == QLatin1String("0.0.0.0") || hostIp == QLatin1String("::") || hostIp == QLatin1String("[::]");
}

/*! Which family a wildcard address belongs to: 4 / 6 (0 when not a wildcard). */
int wildcardFamily(const QString &hostIp)
{
    if (hostIp.isEmpty() || hostIp == QLatin1String("0.0.0.0")) {
        return 4;
    }
    if (hostIp == QLatin1String("::") || hostIp == QLatin1String("[::]")) {
        return 6;
    }
    return 0;
}
} // namespace

void ContainerDetailController::rebuildPorts()
{
    QList<PortMappingEntry> published;
    QList<PortMappingEntry> unpublished;
    for (const Port &port : m_detail.ports) {
        PortMappingEntry entry;
        entry.containerPort = port.privatePort;
        entry.protocol = port.type.isEmpty() ? QStringLiteral("tcp") : port.type;
        entry.hostIp = port.ip;
        entry.hostPort = port.publicPort;
        if (entry.isPublished()) {
            /*
             * Merge IPv4/IPv6 wildcards (user-requested): with no host address given, Docker creates both
             * `0.0.0.0:<port>` and `[::]:<port>`, and two nodes read as "mapped twice". Same container
             * port + same host port + one wildcard of each family -> merge into one row marked dualStack.
             */
            bool merged = false;
            for (PortMappingEntry &existing : published) {
                if (existing.containerPort == entry.containerPort && existing.protocol == entry.protocol
                    && existing.hostPort == entry.hostPort && !existing.dualStack
                    && isWildcardAddress(existing.hostIp) && isWildcardAddress(entry.hostIp)
                    && wildcardFamily(existing.hostIp) != wildcardFamily(entry.hostIp)) {
                    existing.dualStack = true;
                    merged = true;
                    break;
                }
            }
            if (!merged) {
                published.append(entry);
            }
        } else {
            unpublished.append(entry);
        }
    }

    // Stable order: the topology draws its links per row, so jitter shakes the whole graph each refresh
    const auto byContainerPort = [](const PortMappingEntry &lhs, const PortMappingEntry &rhs) {
        if (lhs.containerPort != rhs.containerPort) {
            return lhs.containerPort < rhs.containerPort;
        }
        if (lhs.protocol != rhs.protocol) {
            return lhs.protocol < rhs.protocol;
        }
        return lhs.hostPort < rhs.hostPort;
    };
    std::sort(published.begin(), published.end(), byContainerPort);
    std::sort(unpublished.begin(), unpublished.end(), byContainerPort);

    m_publishedPorts->setMappings(published);
    m_unpublishedPorts->setMappings(unpublished);
    m_portGroups->setEntries(published);
}

QList<MountEntry> ContainerDetailController::mountEntries() const
{
    QList<MountEntry> entries;
    entries.reserve(m_detail.mounts.size());
    for (const ContainerMount &mount : m_detail.mounts) {
        MountEntry entry;
        entry.typeKey = mount.type;
        entry.source = mount.source;
        entry.destination = mount.destination;
        entry.mode = mount.readOnly ? QStringLiteral("ro") : QStringLiteral("rw");
        entry.volumeName = mount.name;
        if (mount.type == QLatin1String("tmpfs") || mount.source.isEmpty()) {
            entry.sourceStateKey = QStringLiteral("notApplicable");
        } else if (!m_hostPaths) {
            // No probe service injected (some tests): do not claim it exists, and offer no open action
            entry.sourceStateKey = QStringLiteral("notApplicable");
        } else {
            switch (m_hostPaths->probe(mount.source)) {
            case HostPathState::Directory:
                entry.sourceStateKey = QStringLiteral("directory");
                break;
            case HostPathState::NotADirectory:
                entry.sourceStateKey = QStringLiteral("notADirectory");
                break;
            case HostPathState::Missing:
                entry.sourceStateKey = QStringLiteral("missing");
                break;
            case HostPathState::NotApplicable:
                entry.sourceStateKey = QStringLiteral("notApplicable");
                break;
            }
        }
        entries.append(entry);
    }
    return entries;
}

void ContainerDetailController::openMountHostPath(int row)
{
    if (row < 0 || row >= m_mounts->count()) {
        return;
    }
    const MountEntry &mount = m_mounts->mounts().at(row);
    if (!mount.isOpenable()) {
        // The button should never appear; if called anyway, give the reason instead of doing nothing
        setMountActionError(i18n("This mount has no host directory to open."));
        return;
    }
    if (!m_hostPaths) {
        setMountActionError(i18n("Opening host directories is not available in this environment."));
        return;
    }
    setMountActionError(QString());
    // The path itself never enters the log (ARCH_V2 §40); the result comes back via openFinished
    m_hostPaths->openDirectory(mount.source);
}

void ContainerDetailController::setMountActionError(const QString &text)
{
    if (m_mountActionError == text) {
        return;
    }
    m_mountActionError = text;
    Q_EMIT mountActionErrorChanged();
}

void ContainerDetailController::dismissMountActionError()
{
    setMountActionError(QString());
}

} // namespace Kontainer
