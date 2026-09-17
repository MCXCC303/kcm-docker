/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/status_controller.h"

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

StatusController::StatusController(DockerBackendInterface *backend, HostPathService *hostPaths, QObject *parent)
    : QObject(parent)
    , m_backend(backend)
    , m_scheduler(new RefreshScheduler(backend, this))
    , m_engine(new EngineStatus(this))
    , m_storage(new StorageStatus(this))
    , m_containerModel(new ContainerModel(this))
    , m_imageModel(new ImageModel(this))
    , m_containerFilter(new ContainerFilterModel(this))
    , m_imageFilter(new ImageFilterModel(this))
    , m_containerDetail(new ContainerDetailController(backend, hostPaths, this))
    , m_imageDetail(new ImageDetailController(backend, this))
    , m_operations(new OperationController(backend, this))
    , m_hostPaths(hostPaths)
    , m_daemonConfig(new DaemonConfigController(this))
{
    Q_ASSERT(m_backend);
    // 注意：backend 的生命周期由调用方负责，这里绝不接管所有权。

    // 代理模型：搜索/过滤/排序状态由代理自己持有，因此后台刷新不会重置用户条件（§32）
    m_containerFilter->setSourceModel(m_containerModel);
    m_imageFilter->setSourceModel(m_imageModel);

    connect(m_backend, &DockerBackendInterface::engineUpdated, this, &StatusController::onEngineUpdated);
    // 配置页需要 /info 里的 SecurityOptions / RegistryConfig.Mirrors / LiveRestoreEnabled，
    // 因此引擎信息一到就同步给配置控制器
    m_daemonConfig->setEngineInfo(m_backend->engineInfo());
    connect(m_backend, &DockerBackendInterface::containersUpdated, this, &StatusController::onContainersUpdated);
    connect(m_backend, &DockerBackendInterface::imagesUpdated, this, &StatusController::onImagesUpdated);
    connect(m_backend, &DockerBackendInterface::storageUpdated, this, &StatusController::onStorageUpdated);
    connect(m_backend, &DockerBackendInterface::loadingChanged, this, &StatusController::onLoadingChanged);
    connect(m_backend, &DockerBackendInterface::sectionFailed, this, &StatusController::onSectionFailed);
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

void StatusController::refresh()
{
    qCDebug(kontainerModel) << "refresh requested";
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

/* ------------------------------------------------------------------------- */
/* backend 信号                                                                */
/* ------------------------------------------------------------------------- */

void StatusController::onEngineUpdated()
{
    m_daemonConfig->setEngineInfo(m_backend->engineInfo());
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
    m_daemonConfig->setRunningContainerCount(runningContainerCount());
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
        Q_EMIT busyChanged();
    }
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
    return (m_busy || !m_refreshRequested) ? EngineState::Loading : EngineState::Unavailable;
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
