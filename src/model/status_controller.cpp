/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/status_controller.h"

#include "backend/kwallet_credential_store.h"
#include "model/registry_auth_controller.h"

#include <KLocalizedString>

#include "logging.h"

#include <QElapsedTimer>
#include "model/docker_error_text.h"
#include "model/port_binding_rules.h"

namespace Kontainer
{

using Section = DockerBackendInterface::Section;

namespace
{
/*! Fast datasets: only their failures feed the stale decision (storage/detail/stats
    have independent lifetimes). */
bool isFast(Section section)
{
    return section == Section::Engine || section == Section::Containers || section == Section::Images;
}
} // namespace

StatusController::StatusController(DockerBackendInterface *backend,
                                   HostPathService *hostPaths,
                                   QObject *parent,
                                   CredentialBackend *credentialBackend,
                                   MountPresetStore *mountPresetStore,
                                   DirectoryPicker *directoryPicker,
                                   ServiceStatusBackend *serviceStatus)
    : QObject(parent)
    , m_backend(backend)
    , m_scheduler(new RefreshScheduler(backend, this))
    , m_engine(new EngineStatus(this))
    , m_storage(new StorageStatus(this))
    , m_containerModel(new ContainerModel(this))
    , m_imageModel(new ImageModel(this))
    , m_volumeModel(new VolumeModel(this))
    , m_volumeFilter(new VolumeFilterModel(this))
    , m_volumeDetail(new VolumeDetailController(backend, this))
    , m_networkModel(new NetworkModel(this))
    , m_networkFilter(new NetworkFilterModel(this))
    , m_networkDetail(new NetworkDetailController(backend, this))
    , m_hostPortModel(new HostPortModel(this))
    , m_hostPortFilter(new HostPortFilterModel(this))
    , m_containerFilter(new ContainerFilterModel(this))
    , m_imageFilter(new ImageFilterModel(this))
    , m_containerDetail(new ContainerDetailController(backend, hostPaths, this))
    , m_imageDetail(new ImageDetailController(backend, this))
    , m_operations(new OperationController(backend, this))
    // Mount presets are this tool's own data (~/.config/kcm_dockerrc), not system settings (§1.5.3)
    // Mount presets: use the injected store (tests/rendering use a temp file, never the real config)
    , m_mountPresets(mountPresetStore ? mountPresetStore : new MountPresetStore({}, this))
    , m_commandHistory(new CommandHistoryStore({}, this))
    , m_createContainer(new CreateContainerController(m_operations, m_mountPresets, backend, m_containerDetail, m_commandHistory, this))
    // Directory picker: use the injected one (tests and offscreen rendering must not show dialogs)
    , m_directoryPicker(directoryPicker ? directoryPicker : new SystemDirectoryPicker(this))
    , m_busyWatchdog(new QTimer(this))
    , m_services(serviceStatus ? serviceStatus : new SystemdServiceStatus(this))
    , m_hostPaths(hostPaths)
    , m_daemonConfigUser(new DaemonConfigController(this))
    , m_daemonConfigSystem(new DaemonConfigController(this))
    // KWallet is the only credential store (ARCH_V5_V8 §2.6): one instance for backend, store, controller
    , m_credentialBackend(credentialBackend ? credentialBackend : new KWalletBackend(this))
    , m_credentialStore(new CredentialStore(m_credentialBackend, this))
    , m_registryAuth(new RegistryAuthController(backend, m_credentialStore, this))
{
    Q_ASSERT(m_backend);
    // The caller owns the backend's lifetime; ownership is never taken here.

    // In-flight watchdog: single shot, started/stopped by onLoadingChanged while busy
    m_busyWatchdog->setSingleShot(true);
    m_busyWatchdog->setInterval(int(std::chrono::duration_cast<std::chrono::milliseconds>(RefreshPolicy::kInFlightWatchdog).count()));
    connect(m_busyWatchdog, &QTimer::timeout, this, &StatusController::onBusyWatchdogTimeout);

    // Re-query service state after a successful action (so the UI shows "stopped"/"started" at once)
    for (DaemonConfigController *config : {m_daemonConfigUser, m_daemonConfigSystem}) {
        connect(config, &DaemonConfigController::serviceControlled, this, [this](const QString &, const QString &, bool success, const QString &) {
            if (success) {
                m_services->query();
            }
        });
    }

    // Service state: the connection key may change after a query ("connected" can become "service down")
    connect(m_services, &ServiceStatusBackend::servicesChanged, this, [this] {
        Q_EMIT serviceStatesChanged();
        updateStates();
    });

    // Proxy models own search/filter/sort, so background refreshes never reset user criteria (§32)
    m_containerFilter->setSourceModel(m_containerModel);
    m_imageFilter->setSourceModel(m_imageModel);
    m_networkFilter->setSourceModel(m_networkModel);
    m_hostPortFilter->setSourceModel(m_hostPortModel);
    // Any filter/search/sort change must invalidate the cached range map, or the map stays stale
    connect(m_hostPortFilter, &HostPortFilterModel::searchTextChanged, this, &StatusController::invalidatePortRanges);
    connect(m_hostPortFilter, &HostPortFilterModel::stateFilterChanged, this, &StatusController::invalidatePortRanges);
    connect(m_hostPortFilter, &HostPortFilterModel::sortKeyChanged, this, &StatusController::invalidatePortRanges);
    connect(m_hostPortFilter, &HostPortFilterModel::countChanged, this, &StatusController::invalidatePortRanges);
    m_volumeFilter->setSourceModel(m_volumeModel);

    connect(m_backend, &DockerBackendInterface::engineUpdated, this, &StatusController::onEngineUpdated);
    // The config page needs SecurityOptions / RegistryConfig.Mirrors / LiveRestoreEnabled from
    // /info, so engine info is forwarded to the config controllers as soon as it arrives
    m_daemonConfigUser->setScope(QStringLiteral("user"));
    m_daemonConfigSystem->setScope(QStringLiteral("system"));
    m_daemonConfigUser->setEngineInfo(m_backend->engineInfo());
    m_daemonConfigSystem->setEngineInfo(m_backend->engineInfo());
    connect(m_backend, &DockerBackendInterface::containersUpdated, this, &StatusController::onContainersUpdated);
    /*
     * inspect arrived: record this container's **declared** host bindings and rebuild the port
     * table. Only when the ports page asked for it (present in m_declaredRequested) — container
     * detail inspects land here too and need no extra work.
     */
    connect(m_backend, &DockerBackendInterface::containerDetailUpdated, this, [this] {
        const ContainerDetail detail = m_backend->containerDetail();
        if (detail.id.isEmpty() || !m_declaredRequested.contains(detail.id)) {
            return;
        }
        m_declaredPorts.insert(detail.id, detail.declaredPorts);
        rebuildPorts();
    });
    connect(m_backend, &DockerBackendInterface::imagesUpdated, this, &StatusController::onImagesUpdated);
    connect(m_backend, &DockerBackendInterface::networksUpdated, this, &StatusController::onNetworksUpdated);
    connect(m_backend, &DockerBackendInterface::volumesUpdated, this, &StatusController::onVolumesUpdated);
    connect(m_backend, &DockerBackendInterface::storageUpdated, this, &StatusController::onStorageUpdated);
    connect(m_backend, &DockerBackendInterface::loadingChanged, this, &StatusController::onLoadingChanged);
    connect(m_backend, &DockerBackendInterface::sectionFailed, this, &StatusController::onSectionFailed);
    // Pulls from private registries use wallet credentials (anonymous when none are stored)
    m_operations->setCredentialStore(m_credentialStore);

    connect(m_scheduler, &RefreshScheduler::stateChanged, this, &StatusController::refreshStateChanged);
    connect(m_scheduler, &RefreshScheduler::autoRefreshEnabledChanged, this, &StatusController::autoRefreshEnabledChanged);

    // Read after write (ARCH_V4 §2.2.4): a successful operation silently reloads the open detail
    // page, otherwise state badges and resource sections lag until the next 30 s re-check
    if (m_hostPaths) {
        connect(m_hostPaths, &HostPathService::openFinished, this, [this](HostPathError error, const QString &detail) {
            switch (error) {
            case HostPathError::None:
                setHostPathError(QString());
                break;
            case HostPathError::Missing:
                setHostPathError(i18n("The host path does not exist."));
                break;
            case HostPathError::NotADirectory:
                setHostPathError(i18n("The host path is not a directory."));
                break;
            case HostPathError::LaunchFailed:
                setHostPathError(i18n("Could not open the file manager: %1", detail));
                break;
            }
        });
    }

    connect(m_operations, &OperationController::containerStateChanged, this, [this](const QString &id) {
        if (m_containerDetail->containerId() == id) {
            m_containerDetail->reload();
        }
    });
}

StatusController::~StatusController() = default;

QString StatusController::endpoint() const
{
    return m_backend->endpointDisplayName();
}

bool StatusController::autoRefreshEnabled() const
{
    return m_scheduler->autoRefreshEnabled();
}

void StatusController::setAutoRefreshEnabled(bool enabled)
{
    m_scheduler->setAutoRefreshEnabled(enabled);
}

int StatusController::autoRefreshInterval() const
{
    return m_scheduler->refreshIntervalMs();
}

int StatusController::storageRefreshInterval() const
{
    return m_scheduler->storageIntervalMs();
}

QDateTime StatusController::lastUpdated() const
{
    return m_scheduler->lastSuccess();
}

bool StatusController::updateFailed() const
{
    return m_scheduler->consecutiveFailures() > 0;
}

bool StatusController::stale() const
{
    return m_scheduler->isStale();
}

void StatusController::loadLowFrequencyListsOnce()
{
    /*
     * Networks and volumes skip the 5 s polling (low-frequency data), but their **counts** appear
     * in tab titles — users saw 0 until the tab was opened, then 4. So they are read once at page
     * init, and afterwards still refresh on demand (tab switch / changes).
     */
    if (m_lowFrequencyLoaded) {
        return;
    }
    m_lowFrequencyLoaded = true;
    m_backend->refreshNetworks();
    m_backend->refreshVolumes(false);
}

void StatusController::rebuildPorts()
{
    // Port table, counts and range map all come from this one data set: rebuilt only here
    const int declaredBefore = declaredNotPublishedCount();
    const int reservedBefore = reservedPortCount();
    const int inUseBefore = inUsePortCount();
    m_hostPortModel->setEntries(HostPortUsage::entriesFor(m_backend->containers(), m_declaredPorts));
    if (declaredNotPublishedCount() != declaredBefore || reservedPortCount() != reservedBefore
        || inUsePortCount() != inUseBefore) {
        Q_EMIT declaredNotPublishedCountChanged();
    }
    invalidatePortRanges();
}

int StatusController::declaredNotPublishedCount() const
{
    int count = 0;
    for (const HostPortEntry &entry : m_hostPortModel->entries()) {
        if (entry.stateKey == QLatin1String("declaredNotPublished")) {
            ++count;
        }
    }
    return count;
}

int StatusController::inUsePortCount() const
{
    // A range (47300-47309) counts as **ports**, not as declared entries
    int count = 0;
    for (const HostPortEntry &entry : m_hostPortModel->entries()) {
        if (entry.stateKey != QLatin1String("inUse")) {
            continue;
        }
        const quint16 last = entry.hostPortEnd != 0 ? entry.hostPortEnd : entry.hostPort;
        count += int(last) - int(entry.hostPort) + 1;
    }
    return count;
}

int StatusController::startTabFromEnvironment() const
{
    bool ok = false;
    const int index = qEnvironmentVariable("KCM_DOCKER_START_TAB").toInt(&ok);
    return ok && index > 0 ? index : 0;
}

int StatusController::reservedPortCount() const
{
    int count = 0;
    for (const HostPortEntry &entry : m_hostPortModel->entries()) {
        if (entry.stateKey == QLatin1String("reserved") || entry.stateKey == QLatin1String("reservedTaken")) {
            ++count;
        }
    }
    return count;
}

void StatusController::invalidatePortRanges()
{
    m_portRangesDirty = true;
    Q_EMIT portRangesChanged();
}

QVariantList StatusController::portRanges() const
{
    if (!m_portRangesDirty) {
        return m_portRanges;
    }
    m_portRangesDirty = false;
    QElapsedTimer timer;
    timer.start();

    /*
     * The map draws **filtered** ports: search box and state filter apply to it as well (users
     * reported the map could not be filtered, leaving two views with separate data). Entries come
     * from the proxy model, so the filter logic exists in one place.
     */
    QList<HostPortEntry> entries;
    entries.reserve(m_hostPortFilter->rowCount());
    for (int row = 0; row < m_hostPortFilter->rowCount(); ++row) {
        const QModelIndex index = m_hostPortFilter->index(row, 0);
        HostPortEntry entry;
        // Rebuild from structured role fields, never by parsing strings (ranges once lost their end)
        entry.hostPort = quint16(index.data(HostPortModel::HostPortRole).toUInt());
        entry.hostPortEnd = quint16(index.data(HostPortModel::RangeEndRole).toUInt());
        entry.hostIp = index.data(HostPortModel::HostIpRole).toString();
        entry.containerPort = quint16(index.data(HostPortModel::ContainerPortRole).toUInt());
        entry.protocol = index.data(HostPortModel::ProtocolRole).toString();
        entry.stateKey = index.data(HostPortModel::StateKeyRole).toString();
        entry.containerId = index.data(HostPortModel::ContainerIdRole).toString();
        entry.containerName = index.data(HostPortModel::ContainerNameRole).toString();
        entries.append(entry);
    }

    QVariantList ranges;
    /*
     * Tile colours resolve filter-first: with a filter selected, only that state is painted; under
     * "all ports" the priority is running > occupied > not started > free, so a port like 20003
     * that is both free and running shows as running (user request).
     */
    const QStringList preferred = m_hostPortFilter->stateFilter() == QLatin1String("all")
        ? QStringList()
        : QStringList {m_hostPortFilter->stateFilter()};
    for (const HostPortRange &range : HostPortUsage::clusterRanges(entries)) {
        QVariantList tiles;
        for (quint16 port = range.first; port < quint16(range.first + range.tileCount); ++port) {
            const HostPortEntry entry = HostPortUsage::entryForPort(entries, port, preferred);
            tiles.append(QVariantMap {{QStringLiteral("port"), int(port)},
                                      {QStringLiteral("text"), QString::number(port)},
                                      {QStringLiteral("stateKey"), entry.stateKey},
                                      {QStringLiteral("occupied"), !entry.stateKey.isEmpty()},
                                      // An in-use port maps to one container, so a map click can jump to it
                                      {QStringLiteral("containerId"), entry.stateKey == QLatin1String("inUse") ? entry.containerId : QString()},
                                      {QStringLiteral("containerName"), entry.stateKey == QLatin1String("inUse") ? entry.containerName : QString()}});
        }
        ranges.append(QVariantMap {{QStringLiteral("first"), int(range.first)},
                                   {QStringLiteral("last"), int(range.last)},
                                   {QStringLiteral("title"), QStringLiteral("%1 – %2").arg(range.first).arg(range.last)},
                                   {QStringLiteral("tileCount"), range.tileCount},
                                   {QStringLiteral("hiddenCount"), range.hiddenCount},
                                   {QStringLiteral("usedCount"), range.usedCount},
                                   {QStringLiteral("tiles"), tiles}});
    }
    m_portRanges = ranges;
    qCDebug(kontainerModel) << "rebuilt port ranges:" << ranges.size() << "ranges in" << timer.elapsed() << "ms";
    return m_portRanges;
}

int StatusController::nextFreeHostPort() const
{
    return HostPortUsage::nextFreePort(m_backend->containers(), 0);
}

void StatusController::refreshPorts()
{
    m_backend->refreshContainers();
    /*
     * Declarations come from inspect, and **every** container is inspected:
     *   - running: to tell "declared but not actually published" (`declaredNotPublished`);
     *   - stopped: to mark "the port is free now, but that container takes it back when it starts"
     *     (`reserved`).
     * Users asked for both (the earlier "no reserved" decision was reversed, see ARCH_next_ports.md).
     * Each container is inspected once per page visit (`m_declaredRequested`), not per view.
     */
    for (const Container &container : m_backend->containers()) {
        if (container.id.isEmpty() || m_declaredRequested.contains(container.id)) {
            continue;
        }
        m_declaredRequested.append(container.id);
        m_backend->inspectContainer(container.id);
    }
}

void StatusController::refreshNetworks()
{
    m_backend->refreshNetworks();
}

void StatusController::refreshVolumes(bool includeUsage)
{
    m_backend->refreshVolumes(includeUsage);
}

void StatusController::refresh()
{
    loadLowFrequencyListsOnce();
    qCDebug(kontainerModel) << "refresh requested";
    // After a manual refresh "last operation succeeded" hints are outdated (A7); failures stay,
    // since they are often the only "why" the user can see (decided in dismissResultIfObsolete)
    m_operations->dismissResultIfObsolete();
    m_services->query();
    m_refreshRequested = true;
    m_scheduler->requestRefresh(RefreshScheduler::Reason::Manual);
    m_backend->refreshStorageUsage();
}

void StatusController::retryStorage()
{
    m_backend->refreshStorageUsage();
}

/* ------------------------------------------------------------------------- */
/* State keys (QML contract)                                                 */
/* ------------------------------------------------------------------------- */

QString StatusController::stateKeyFor(State state)
{
    switch (state) {
    case State::Idle:
        return QStringLiteral("idle");
    case State::Loading:
        return QStringLiteral("loading");
    case State::Ready:
        return QStringLiteral("ready");
    case State::Error:
        return QStringLiteral("error");
    }
    return QStringLiteral("idle");
}

QString StatusController::engineStateKeyFor(EngineState state)
{
    switch (state) {
    case EngineState::Loading:
        return QStringLiteral("loading");
    case EngineState::Ready:
        return QStringLiteral("ready");
    case EngineState::Partial:
        return QStringLiteral("partial");
    case EngineState::Refreshing:
        return QStringLiteral("refreshing");
    case EngineState::Unavailable:
        return QStringLiteral("unavailable");
    }
    return QStringLiteral("unavailable");
}

QString StatusController::listStateKeyFor(ListState state)
{
    switch (state) {
    case ListState::Idle:
        return QStringLiteral("idle");
    case ListState::Loading:
        return QStringLiteral("loading");
    case ListState::Ready:
        return QStringLiteral("ready");
    case ListState::Empty:
        return QStringLiteral("empty");
    case ListState::Error:
        return QStringLiteral("error");
    }
    return QStringLiteral("idle");
}

QString StatusController::buildStamp() const
{
    return QStringLiteral(KCM_DOCKER_BUILD_STAMP);
}

QString StatusController::stateKey() const
{
    return stateKeyFor(m_state);
}

QString StatusController::engineStateKey() const
{
    return engineStateKeyFor(m_engineState);
}

QString StatusController::connectionKey() const
{
    /*
     * Refined connection state (report B1):
     *
     * With docker.service stopped, `docker.socket` is still there (socket activation), so looking at
     * the socket alone reports "connected" while the daemon is down and every action fails. Service
     * state is therefore included: when not all services run, the connection key says "services
     * down" so the UI can use a different hint and colour. `containerd.service` does not take part
     * in the "connected" decision (it serves no Docker API), it only flags missing capabilities.
     */
    const bool dockerSocket = m_services->isActive(QStringLiteral("docker.socket"));
    const bool dockerService = m_services->isActive(QStringLiteral("docker.service"));
    const bool servicesDown = !dockerSocket || !dockerService;
    /*
     * "Connected" requires the **last refresh to have succeeded**: cached engine data alone
     * misleads — after the daemon stops the old data remains and the UI says "connected" while
     * every action fails.
     */
    const bool engineDataUsable = m_engineState == EngineState::Ready || m_engineState == EngineState::Refreshing
        || m_engineState == EngineState::Partial;
    const bool connected = engineDataUsable && !updateFailed();
    if (connected) {
        return servicesDown ? QStringLiteral("connectedServicesDown") : QStringLiteral("connected");
    }
    return servicesDown ? QStringLiteral("disconnectedServicesDown") : QStringLiteral("disconnected");
}

QString StatusController::engineStateSemanticKey() const
{
    // Partial = connected but the /info summary is unreadable: degraded (warning), not a failure.
    switch (m_engineState) {
    case EngineState::Ready:
    case EngineState::Refreshing:
        return QStringLiteral("positive");
    case EngineState::Partial:
        return QStringLiteral("neutral");
    case EngineState::Loading:
        return QStringLiteral("disabled");
    case EngineState::Unavailable:
        return QStringLiteral("negative");
    }
    return QStringLiteral("disabled");
}

QString StatusController::engineStateIconName() const
{
    switch (m_engineState) {
    case EngineState::Ready:
    case EngineState::Refreshing:
        return QStringLiteral("dialog-ok-apply");
    case EngineState::Partial:
        return QStringLiteral("data-warning");
    case EngineState::Loading:
        return QStringLiteral("chronometer");
    case EngineState::Unavailable:
        return QStringLiteral("dialog-error");
    }
    return QStringLiteral("dialog-question");
}

QString StatusController::containersStateKey() const
{
    return listStateKeyFor(m_containersState);
}

QString StatusController::imagesStateKey() const
{
    return listStateKeyFor(m_imagesState);
}

QString StatusController::storageStateKey() const
{
    return listStateKeyFor(m_storageState);
}

QString StatusController::networksStateKey() const
{
    return listStateKeyFor(m_networksState);
}

QString StatusController::volumesStateKey() const
{
    return listStateKeyFor(m_volumesState);
}

/* ------------------------------------------------------------------------- */
/* backend signals                                                           */
/* ------------------------------------------------------------------------- */

void StatusController::onEngineUpdated()
{
    m_daemonConfigUser->setEngineInfo(m_backend->engineInfo());
    m_daemonConfigSystem->setEngineInfo(m_backend->engineInfo());
    const EngineInfo info = m_backend->engineInfo();
    if (info.available) {
        m_engine->setInfo(info);
    } else {
        m_engine->clear();
    }

    m_engineOk = info.available;
    m_engineFailed = !info.available;
    if (info.available && info.countsAvailable) {
        setSectionError(Section::Engine, QString());
    }
    updateStates();
}

void StatusController::onContainersUpdated()
{
    m_daemonConfigUser->setRunningContainerCount(runningContainerCount());
    m_daemonConfigSystem->setRunningContainerCount(runningContainerCount());
    m_containerModel->setContainers(m_backend->containers());
    // The port view follows the container list: publications come from it, declarations from inspect
    rebuildPorts();
    m_containersOk = true;
    m_containersFailed = false;
    setSectionError(Section::Containers, QString());
    m_scheduler->noteFastUpdateSucceeded();
    updateStates();
}

void StatusController::onImagesUpdated()
{
    m_imageModel->setImages(m_backend->images());
    m_imagesOk = true;
    m_imagesFailed = false;
    setSectionError(Section::Images, QString());
    m_scheduler->noteFastUpdateSucceeded();
    updateStates();
}

void StatusController::onNetworksUpdated()
{
    m_networkModel->setNetworks(m_backend->networks());
    if (!m_networksOk) {
        m_networksOk = true;
        m_networksFailed = false;
        updateStates();
    }
    if (!m_networksError.isEmpty()) {
        m_networksError.clear();
        Q_EMIT networksErrorChanged();
    }
}

void StatusController::onVolumesUpdated()
{
    m_volumeModel->setVolumes(m_backend->volumes());
    if (!m_volumesOk) {
        m_volumesOk = true;
        m_volumesFailed = false;
        updateStates();
    }
    if (!m_volumesError.isEmpty()) {
        m_volumesError.clear();
        Q_EMIT volumesErrorChanged();
    }
}

void StatusController::onStorageUpdated()
{
    m_storage->setUsage(m_backend->storageUsage());
    m_storageOk = true;
    m_storageFailed = false;
    setSectionError(Section::Storage, QString());
    updateStates();
}

void StatusController::onLoadingChanged()
{
    const bool busy = m_backend->isLoading();
    if (busy != m_busy) {
        m_busy = busy;
        if (busy) {
            // In-flight request started: arm the watchdog (on timeout it is abandoned, UI can retry)
            m_busyWatchdog->start();
        } else {
            m_busyWatchdog->stop();
        }
        Q_EMIT busyChanged();
    }
    updateStates();
}

void StatusController::requestAutomaticRefreshForTesting()
{
    m_scheduler->requestRefresh(RefreshScheduler::Reason::Automatic);
}

void StatusController::setInFlightWatchdogMs(int milliseconds)
{
    m_busyWatchdog->setInterval(milliseconds > 0 ? milliseconds : 1);
}

int StatusController::inFlightWatchdogMs() const
{
    return m_busyWatchdog->interval();
}

void StatusController::onBusyWatchdogTimeout()
{
    if (!m_busy) {
        m_busyWatchdog->stop();
        return;
    }
    // Fallback for requests stuck "never returning" (half-dead daemon, socket accepted but silent).
    // Abandon them and report the timeout, or the UI stays on "loading / backend busy" (B3/B4).
    m_timeoutReason = i18n("Docker stopped responding; the pending request was given up. Retry when the service is available again.");
    m_backend->abandonInFlightRequests(
        DockerError(DockerError::Kind::Timeout, QStringLiteral("no response within the watchdog window")));
    m_busyWatchdog->stop();
    updateStates();
}

void StatusController::onSectionFailed(Section section, const DockerError &error)
{
    setSectionError(section, dockerErrorText(error));
    switch (section) {
    case Section::Engine:
        m_engineOk = false;
        m_engineFailed = true;
        break;
    case Section::Containers:
        m_containersOk = false;
        m_containersFailed = true;
        break;
    case Section::Images:
        m_imagesOk = false;
        m_imagesFailed = true;
        break;
    case Section::Storage:
        m_storageOk = false;
        m_storageFailed = true;
        m_storage->clear(); // Do not let old data pass as fresh on failure (§15)
        break;
    case Section::Networks:
        // Networks are low-frequency: on failure **keep** the previous list (the backend does too),
        // only mark the state as failed and show a hint, so the UI does not suddenly go empty
        m_networksOk = false;
        m_networksFailed = true;
        break;
    case Section::Volumes:
        // Same for volumes: keep the list, only mark the failure
        m_volumesOk = false;
        m_volumesFailed = true;
        break;
    case Section::ContainerDetail:
    case Section::ImageDetail:
    case Section::Stats:
        // Detail/stats failures belong to their own controllers and spare overview and lists (§30/§31)
        break;
    }

    if (isFast(section)) {
        m_scheduler->noteFastUpdateFailed();
    }
    qCWarning(kontainerModel) << "section failed:" << int(section) << error.detail();
    updateStates();
}

void StatusController::setSectionError(Section section, const QString &text)
{
    switch (section) {
    case Section::Engine:
        if (m_engineError == text) {
            return;
        }
        m_engineError = text;
        Q_EMIT engineErrorChanged();
        return;
    case Section::Containers:
        if (m_containersError == text) {
            return;
        }
        m_containersError = text;
        Q_EMIT containersErrorChanged();
        return;
    case Section::Images:
        if (m_imagesError == text) {
            return;
        }
        m_imagesError = text;
        Q_EMIT imagesErrorChanged();
        return;
    case Section::Storage:
        if (m_storageError == text) {
            return;
        }
        m_storageError = text;
        Q_EMIT storageErrorChanged();
        return;
    case Section::Networks:
        if (m_networksError == text) {
            return;
        }
        m_networksError = text;
        Q_EMIT networksErrorChanged();
        return;
    case Section::Volumes:
        if (m_volumesError == text) {
            return;
        }
        m_volumesError = text;
        Q_EMIT volumesErrorChanged();
        return;
    case Section::ContainerDetail:
    case Section::ImageDetail:
    case Section::Stats:
        return;
    }
}

/* ------------------------------------------------------------------------- */
/* State machine (§14/§30)                                                   */
/* ------------------------------------------------------------------------- */

StatusController::EngineState StatusController::computeEngineState() const
{
    if (m_engine->available()) {
        if (!m_engineError.isEmpty()) {
            return EngineState::Partial; // connected, but the /info summary is unavailable
        }
        return m_busy ? EngineState::Refreshing : EngineState::Ready;
    }
    /*
     * Three not-yet-connected cases must be told apart (report B3: with the daemon down the UI
     * stayed on "loading" forever):
     *  - no refresh requested yet → Loading
     *  - requested, still in flight → Loading
     *  - requested and already failed → Unavailable (with a reason and a retry action)
     * The old check only asked "requested or not", so an unreachable daemon loaded forever.
     */
    if (!m_refreshRequested) {
        return EngineState::Loading;
    }
    if (m_busy) {
        return EngineState::Loading;
    }
    return EngineState::Unavailable;
}

StatusController::ListState StatusController::computeListState(bool ok, bool failed, int count) const
{
    if (failed) {
        return ListState::Error;
    }
    if (ok) {
        return count == 0 ? ListState::Empty : ListState::Ready;
    }
    return m_busy ? ListState::Loading : ListState::Idle;
}

StatusController::ListState StatusController::computeStorageState() const
{
    if (m_storageFailed) {
        return ListState::Error;
    }
    if (m_storageOk) {
        return ListState::Ready;
    }
    return m_busy ? ListState::Loading : ListState::Idle;
}

void StatusController::updateStates()
{
    const EngineState engineState = computeEngineState();
    if (engineState != m_engineState) {
        m_engineState = engineState;
        Q_EMIT engineStateChanged();
    }

    const ListState containersState = computeListState(m_containersOk, m_containersFailed, m_containerModel->count());
    if (containersState != m_containersState) {
        m_containersState = containersState;
        Q_EMIT containersStateChanged();
    }

    const ListState imagesState = computeListState(m_imagesOk, m_imagesFailed, m_imageModel->count());
    if (imagesState != m_imagesState) {
        m_imagesState = imagesState;
        Q_EMIT imagesStateChanged();
    }

    const ListState storageState = computeStorageState();
    if (storageState != m_storageState) {
        m_storageState = storageState;
        Q_EMIT storageStateChanged();
    }

    // The network list does not drive the page state (§30): no data means an empty networks page
    const ListState networksState = computeListState(m_networksOk, m_networksFailed, m_networkModel->count());
    if (networksState != m_networksState) {
        m_networksState = networksState;
        Q_EMIT networksStateChanged();
    }

    const ListState volumesState = computeListState(m_volumesOk, m_volumesFailed, m_volumeModel->count());
    if (volumesState != m_volumesState) {
        m_volumesState = volumesState;
        Q_EMIT volumesStateChanged();
    }

    State next = State::Idle;
    const bool anyOk = m_engineOk || m_containersOk || m_imagesOk;
    const bool anyFailed = m_engineFailed || m_containersFailed || m_imagesFailed;
    if (m_busy) {
        next = State::Loading;
    } else if (anyOk) {
        next = State::Ready;
    } else if (anyFailed) {
        // Only when all fast datasets fail does the page enter Error (§30: storage/detail do not count)
        next = State::Error;
    }

    if (next != m_state) {
        m_state = next;
        Q_EMIT stateChanged();
    }
}

QString StatusController::hostPathStateKey(const QString &path) const
{
    if (path.isEmpty() || !m_hostPaths) {
        return QStringLiteral("notApplicable");
    }
    switch (m_hostPaths->probe(path)) {
    case HostPathState::Directory:
        return QStringLiteral("directory");
    case HostPathState::NotADirectory:
        return QStringLiteral("notADirectory");
    case HostPathState::Missing:
        return QStringLiteral("missing");
    case HostPathState::NotApplicable:
        break;
    }
    return QStringLiteral("notApplicable");
}

bool StatusController::openHostPath(const QString &path)
{
    if (!m_hostPaths) {
        setHostPathError(i18n("Opening host paths is not available in this environment."));
        return false;
    }
    setHostPathError(QString());
    // The path itself is never logged (ARCH_V2 §40); the result arrives via openFinished
    return m_hostPaths->openDirectory(path);
}

int StatusController::runningContainerCount() const
{
    int running = 0;
    if (!m_backend) {
        return running;
    }
    for (const Container &container : m_backend->containers()) {
        if (container.stateKey() == QLatin1String("running")) {
            ++running;
        }
    }
    return running;
}

QStringList StatusController::portBindingsInUse() const
{
    QStringList bindings;
    if (!m_backend) {
        return bindings;
    }
    for (const Container &container : m_backend->containers()) {
        for (const Port &port : container.ports) {
            if (!port.isPublished()) {
                continue;
            }
            const QString ip = port.ip.isEmpty() ? QStringLiteral("0.0.0.0") : port.ip;
            bindings.append(ip + QLatin1Char(':') + QString::number(port.publicPort));
        }
    }
    return bindings;
}

void StatusController::setHostPathError(const QString &text)
{
    if (m_hostPathError == text) {
        return;
    }
    m_hostPathError = text;
    Q_EMIT hostPathErrorChanged();
}

} // namespace Kontainer
