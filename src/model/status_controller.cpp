/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/status_controller.h"

#include "backend/kwallet_credential_store.h"
#include "model/registry_auth_controller.h"

#include <KLocalizedString>

#include "logging.h"
#include "model/docker_error_text.h"

namespace Kontainer
{

using Section = DockerBackendInterface::Section;

namespace
{
/*! 高频数据集：它们的失败才影响 stale 判定（storage/detail/stats 是独立生命周期）。 */
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
    , m_containerFilter(new ContainerFilterModel(this))
    , m_imageFilter(new ImageFilterModel(this))
    , m_containerDetail(new ContainerDetailController(backend, hostPaths, this))
    , m_imageDetail(new ImageDetailController(backend, this))
    , m_operations(new OperationController(backend, this))
    // 挂载预设是"这个工具的数据"（~/.config/kontainerrc），不是系统设置（§1.5.3）
    // 挂载预设：注入时用注入的（测试/渲染用临时文件，绝不写用户真实配置）
    , m_mountPresets(mountPresetStore ? mountPresetStore : new MountPresetStore({}, this))
    , m_commandHistory(new CommandHistoryStore({}, this))
    , m_createContainer(new CreateContainerController(m_operations, m_mountPresets, backend, m_containerDetail, m_commandHistory, this))
    // 目录选择：注入时用注入的（测试与离屏渲染不弹真实对话框）
    , m_directoryPicker(directoryPicker ? directoryPicker : new PortalDirectoryPicker(this))
    , m_busyWatchdog(new QTimer(this))
    , m_services(serviceStatus ? serviceStatus : new SystemdServiceStatus(this))
    , m_hostPaths(hostPaths)
    , m_daemonConfigUser(new DaemonConfigController(this))
    , m_daemonConfigSystem(new DaemonConfigController(this))
    // 凭据的唯一持久化位置是 KWallet（ARCH_V5_V8 §2.6）：后端、存储、控制器各一处实例
    , m_credentialBackend(credentialBackend ? credentialBackend : new KWalletBackend(this))
    , m_credentialStore(new CredentialStore(m_credentialBackend, this))
    , m_registryAuth(new RegistryAuthController(backend, m_credentialStore, this))
{
    Q_ASSERT(m_backend);
    // 注意：backend 的生命周期由调用方负责，这里绝不接管所有权。

    // 在途看门狗：单次触发，busy 期间由 onLoadingChanged 启动/停止
    m_busyWatchdog->setSingleShot(true);
    m_busyWatchdog->setInterval(int(std::chrono::duration_cast<std::chrono::milliseconds>(RefreshPolicy::kInFlightWatchdog).count()));
    connect(m_busyWatchdog, &QTimer::timeout, this, &StatusController::onBusyWatchdogTimeout);

    // 服务动作成功后重新查询状态（界面因此立刻看到"已停止/已启动"）
    for (DaemonConfigController *config : {m_daemonConfigUser, m_daemonConfigSystem}) {
        connect(config, &DaemonConfigController::serviceControlled, this, [this](const QString &, const QString &, bool success, const QString &) {
            if (success) {
                m_services->query();
            }
        });
    }

    // 服务状态：查询回来后连接 key 可能变化（"已连接"要能因为服务停了而变成"服务未运行"）
    connect(m_services, &ServiceStatusBackend::servicesChanged, this, [this] {
        Q_EMIT serviceStatesChanged();
        updateStates();
    });

    // 代理模型：搜索/过滤/排序状态由代理自己持有，因此后台刷新不会重置用户条件（§32）
    m_containerFilter->setSourceModel(m_containerModel);
    m_imageFilter->setSourceModel(m_imageModel);
    m_networkFilter->setSourceModel(m_networkModel);
    m_volumeFilter->setSourceModel(m_volumeModel);

    connect(m_backend, &DockerBackendInterface::engineUpdated, this, &StatusController::onEngineUpdated);
    // 配置页需要 /info 里的 SecurityOptions / RegistryConfig.Mirrors / LiveRestoreEnabled，
    // 因此引擎信息一到就同步给配置控制器
    m_daemonConfigUser->setScope(QStringLiteral("user"));
    m_daemonConfigSystem->setScope(QStringLiteral("system"));
    m_daemonConfigUser->setEngineInfo(m_backend->engineInfo());
    m_daemonConfigSystem->setEngineInfo(m_backend->engineInfo());
    connect(m_backend, &DockerBackendInterface::containersUpdated, this, &StatusController::onContainersUpdated);
    connect(m_backend, &DockerBackendInterface::imagesUpdated, this, &StatusController::onImagesUpdated);
    connect(m_backend, &DockerBackendInterface::networksUpdated, this, &StatusController::onNetworksUpdated);
    connect(m_backend, &DockerBackendInterface::volumesUpdated, this, &StatusController::onVolumesUpdated);
    connect(m_backend, &DockerBackendInterface::storageUpdated, this, &StatusController::onStorageUpdated);
    connect(m_backend, &DockerBackendInterface::loadingChanged, this, &StatusController::onLoadingChanged);
    connect(m_backend, &DockerBackendInterface::sectionFailed, this, &StatusController::onSectionFailed);
    // 拉取私有仓库时用钱包里的凭据（没有就是匿名拉取）
    m_operations->setCredentialStore(m_credentialStore);

    connect(m_scheduler, &RefreshScheduler::stateChanged, this, &StatusController::refreshStateChanged);
    connect(m_scheduler, &RefreshScheduler::autoRefreshEnabledChanged, this, &StatusController::autoRefreshEnabledChanged);

    // 写后即读（ARCH_V4 §2.2.4）：操作成功后让打开着的详情页静默重读，
    // 否则状态徽标与资源分区要等到下一次 30 秒复核才会跟上
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
    qCDebug(kontainerModel) << "refresh requested";
    // 用户主动刷新后，"上一次操作成功"这类提示已经过时（A7）：
    // 失败类信息保留，因为它往往是用户唯一能看到的"为什么"（dismissResultIfObsolete 里判断）
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
/* 状态 key（QML 契约）                                                        */
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
    return QStringLiteral(KONTAINER_BUILD_STAMP);
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
     * 连接状态的细化（用户实测 B1）：
     *
     * `docker.service` 停掉时 `docker.socket` 还在（socket 激活的语义），只看 socket 会显示
     * "已连接"——但守护进程已经停了，用户点任何操作都不会成功。因此把服务状态一并纳入：
     * 服务未全部运行时，连接状态明确带上"服务未运行"，界面据此给不同的提示与颜色。
     * `containerd.service` 不参与"已连接"的判定（它不提供 Docker API），只用于提示部分能力可用。
     */
    const bool dockerSocket = m_services->isActive(QStringLiteral("docker.socket"));
    const bool dockerService = m_services->isActive(QStringLiteral("docker.service"));
    const bool servicesDown = !dockerSocket || !dockerService;
    /*
     * "已连接"要求**最近一次刷新是成功的**：只看缓存的引擎数据会误导——
     * 守护进程停掉之后我们仍留着上一次读到的数据，用户看到"已连接"却点什么都失败。
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
    // Partial = 已连接但 /info 概要读不到：降级（警告），不是失败。
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
/* backend 信号                                                                */
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
            // 在途请求开始：起看门狗（超时后放弃在途请求，界面回到"可以重试"）
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
    // 兜底：请求卡在"永远不回来"的状态（daemon 半死不活、socket 接了不回数据）。
    // 放弃在途请求并如实报告超时——否则界面会永久停在"正在加载 / backend busy"（B3/B4）。
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
        m_storage->clear(); // 失败时不让旧数据继续冒充最新（§15）
        break;
    case Section::Networks:
        // 网络是低频数据：读失败时**保留**上一次的列表（后端也保留），
        // 只把状态标成失败并在页面上提示，避免界面突然空掉
        m_networksOk = false;
        m_networksFailed = true;
        break;
    case Section::Volumes:
        // 数据卷同理：保留列表，只标失败
        m_volumesOk = false;
        m_volumesFailed = true;
        break;
    case Section::ContainerDetail:
    case Section::ImageDetail:
    case Section::Stats:
        // 详情/采样失败由各自的 controller 处理，不影响概览与列表（§30/§31）
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
/* 状态机（§14/§30）                                                           */
/* ------------------------------------------------------------------------- */

StatusController::EngineState StatusController::computeEngineState() const
{
    if (m_engine->available()) {
        if (!m_engineError.isEmpty()) {
            return EngineState::Partial; // 已连接，但汇总信息（/info）不可用
        }
        return m_busy ? EngineState::Refreshing : EngineState::Ready;
    }
    /*
     * 还没连上时的三种情况要分清（用户实测 B3：服务没起来时界面永远停在"正在加载"）：
     *  - 还没请求过刷新 → Loading
     *  - 请求过、仍在途     → Loading
     *  - 请求过、已经失败过 → Unavailable（带错误原因，并且可以"重试"）
     * 原来的判定只看"请求过没有"，于是 daemon 不可用时永远显示正在加载。
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

    // 网络列表不参与整页状态（§30）：它没有数据时只是"网络页空着"
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
        // 只有高频数据集全部失败才让整页进入 Error（§30：storage/detail 失败不影响整页）
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
    // 路径本身不进日志（ARCH_V2 §40）；结果经 openFinished 回来
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
