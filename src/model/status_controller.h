/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/docker_backend_interface.h"
#include "model/container_detail_controller.h"
#include "model/container_filter_model.h"
#include "model/container_model.h"
#include "model/engine_status.h"
#include "model/image_detail_controller.h"
#include "model/image_filter_model.h"
#include "backend/host_path_service.h"
#include "backend/service_status.h"
#include "backend/credential_store.h"
#include "backend/directory_picker.h"
#include "model/daemon_config_controller.h"
#include "model/operation_controller.h"
#include "model/registry_auth_controller.h"
#include "model/image_model.h"
#include "model/network_filter_model.h"
#include "model/network_detail_controller.h"
#include "model/host_port_filter_model.h"
#include "model/host_port_model.h"
#include "model/network_model.h"
#include "model/command_history_store.h"
#include "model/create_container_controller.h"
#include "model/mount_preset_store.h"
#include "model/volume_detail_controller.h"
#include "model/volume_filter_model.h"
#include "model/volume_model.h"
#include "model/refresh_scheduler.h"
#include "refresh_policy.h"
#include "model/storage_status.h"

#include <QDateTime>
#include <QTimer>
#include <QObject>
#include <QString>

namespace Kontainer
{

/*!
 * Presentation controller between the KCM and the backend (ARCH_V1 §6.2/§14, ARCH_V2 §13–§16/§30/§43).
 *
 * Responsibilities:
 *  - Move backend domain data into QML-bindable models (containers / images / engine / storage / detail)
 *  - Maintain an explicit UI state machine: whole-page State plus per-section state keys, so QML
 *    compares a single value
 *  - Isolate section failures: storage / detail / metrics failures never put the page into Error (§30)
 *  - Last Updated / Stale / partial-failure reporting (§15/§16)
 *  - Delegate refresh scheduling to RefreshScheduler (§14); no page-level QTimer lives here
 *
 * Not responsible for: Docker URL building, JSON parsing, HTTP status codes, socket access,
 * page navigation (§43).
 */
class StatusController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(Kontainer::StatusController::State state READ state NOTIFY stateChanged)
    Q_PROPERTY(Kontainer::StatusController::EngineState engineState READ engineState NOTIFY engineStateChanged)
    Q_PROPERTY(Kontainer::StatusController::ListState containersState READ containersState NOTIFY containersStateChanged)
    Q_PROPERTY(Kontainer::StatusController::ListState imagesState READ imagesState NOTIFY imagesStateChanged)
    Q_PROPERTY(Kontainer::StatusController::ListState storageState READ storageState NOTIFY storageStateChanged)
    /*! QML uses string state keys only: with several Q_ENUMs sharing member names inside one
     * class, Type.Loading resolves to the wrong enum. */
    Q_PROPERTY(QString stateKey READ stateKey NOTIFY stateChanged)
    Q_PROPERTY(QString engineStateKey READ engineStateKey NOTIFY engineStateChanged)
    Q_PROPERTY(QString containersStateKey READ containersStateKey NOTIFY containersStateChanged)
    Q_PROPERTY(QString imagesStateKey READ imagesStateKey NOTIFY imagesStateChanged)
    Q_PROPERTY(QString storageStateKey READ storageStateKey NOTIFY storageStateChanged)
    Q_PROPERTY(QString networksStateKey READ networksStateKey NOTIFY networksStateChanged)
    Q_PROPERTY(QString volumesStateKey READ volumesStateKey NOTIFY volumesStateChanged)
    /*!
     * Semantics and icon of the engine connection state (ARCH_V3 §2.1: semantics belong to the
     * model layer, so QML maps a key to a theme colour instead of switching on strings).
     */
    Q_PROPERTY(QString engineStateSemanticKey READ engineStateSemanticKey NOTIFY engineStateChanged)
    Q_PROPERTY(QString engineStateIconName READ engineStateIconName NOTIFY engineStateChanged)

    /*!
     * Debug start tab (`KCM_DOCKER_START_TAB=<index>`).
     *
     * QML cannot read environment variables, so it is read once here: it only makes "open straight
     * to a page" (screenshot review / debugging) possible. Default 0 = containers, unchanged.
     */
    Q_PROPERTY(int startTabFromEnvironment READ startTabFromEnvironment CONSTANT)

    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString engineError READ engineError NOTIFY engineErrorChanged)
    Q_PROPERTY(QString containersError READ containersError NOTIFY containersErrorChanged)
    Q_PROPERTY(QString imagesError READ imagesError NOTIFY imagesErrorChanged)
    Q_PROPERTY(QString storageError READ storageError NOTIFY storageErrorChanged)
    Q_PROPERTY(QString networksError READ networksError NOTIFY networksErrorChanged)
    /*! Volume list (phase 6 §3.5): low-frequency data, refreshed when entering the page. */
    Q_PROPERTY(ListState volumesState READ volumesState NOTIFY volumesStateChanged)
    Q_PROPERTY(QString volumesError READ volumesError NOTIFY volumesErrorChanged)

    /*! The endpoint cannot change at runtime (no config write path), hence CONSTANT. */
    Q_PROPERTY(QString endpoint READ endpoint CONSTANT)
    /*!
     * Build stamp (version + short git hash + build time).
     *
     * When debugging real sessions, "which build is the user running" must be answerable at a
     * glance — once install and relink were 0.3 s apart and a crash could not be matched to code.
     */
    Q_PROPERTY(QString buildStamp READ buildStamp CONSTANT)

    Q_PROPERTY(bool autoRefreshEnabled READ autoRefreshEnabled WRITE setAutoRefreshEnabled NOTIFY autoRefreshEnabledChanged)
    Q_PROPERTY(int autoRefreshInterval READ autoRefreshInterval CONSTANT)
    Q_PROPERTY(int storageRefreshInterval READ storageRefreshInterval CONSTANT)

    /*! Network list and its filter proxy (phase 6 §3.2). */
    Q_PROPERTY(Kontainer::NetworkModel *networkModel READ networkModel CONSTANT)
    /*! Host port view (ARCH_next_ports.md §4.A): actual publications from the container list,
     * plus declarations of running containers. */
    Q_PROPERTY(Kontainer::HostPortModel *portModel READ portModel CONSTANT)
    /*! Search/filter/sort proxy for the ports page. */
    Q_PROPERTY(Kontainer::HostPortFilterModel *hostPortList READ hostPortList CONSTANT)
    /*! Rows "declared but not published" (running containers; the ports page shows a note). */
    Q_PROPERTY(int declaredNotPublishedCount READ declaredNotPublishedCount NOTIFY declaredNotPublishedCountChanged)
    /*! Rows "declared, container not running" (the port is free now but returns on start). */
    Q_PROPERTY(int reservedPortCount READ reservedPortCount NOTIFY declaredNotPublishedCountChanged)
    /*!
     * Host ports actually held by **running** containers (used by the tab title, untouched by filters).
     *
     * Users reported the old row count also counted "declared" ports; the tab must show how many
     * are really occupied right now.
     */
    Q_PROPERTY(int inUsePortCount READ inUsePortCount NOTIFY declaredNotPublishedCountChanged)
    /*!
     * Range map data (ARCH_next_ports.md §4.B), one entry per range:
     * `{first, last, title, tileCount, hiddenCount, usedCount, tiles: [{port, stateKey}]}`.
     *
     * A **property** rather than a `Q_INVOKABLE`: function calls create no QML dependency, so the
     * map would not update when the port table changes.
     */
    Q_PROPERTY(QVariantList portRanges READ portRanges NOTIFY portRangesChanged)
    /*! Next free host port (shown in the map view for easy copying; 0 = none found). */
    Q_PROPERTY(int nextFreeHostPort READ nextFreeHostPort NOTIFY portRangesChanged)
    Q_PROPERTY(Kontainer::NetworkFilterModel *networkList READ networkList CONSTANT)
    /*! Volume list and its filter proxy. */
    Q_PROPERTY(Kontainer::VolumeModel *volumeModel READ volumeModel CONSTANT)
    Q_PROPERTY(Kontainer::VolumeFilterModel *volumeList READ volumeList CONSTANT)
    /*! Volume detail (labels and driver options of the selected volume). */
    Q_PROPERTY(Kontainer::VolumeDetailController *volumeDetail READ volumeDetail CONSTANT)
    /*! Create-container wizard state and validation (phase 7 §4.4). */
    Q_PROPERTY(Kontainer::CreateContainerController *createContainer READ createContainer CONSTANT)
    /*! Mount presets (phase 7 §4.1): add/edit/remove in the UI and save from a container. */
    Q_PROPERTY(Kontainer::MountPresetStore *mountPresets READ mountPresets CONSTANT)
    /*! Directory picker (for mount preset host paths; tests and rendering inject a stub). */
    Q_PROPERTY(Kontainer::DirectoryPicker *directoryPicker READ directoryPicker CONSTANT)
    /*! State of the three systemd units (B1): connection state and "service not running" hints. */
    Q_PROPERTY(Kontainer::ServiceStatusBackend *services READ services CONSTANT)
    /*!
     * Refined connection key (B1):
     * `connected` / `connectedServicesDown` / `disconnected` / `disconnectedServicesDown`.
     *
     * Why the socket alone is not enough: with docker.service stopped, `docker.socket` is still
     * there (socket activation), so the old code kept showing "connected" — misleading to users
     * (report B1).
     */
    Q_PROPERTY(QString connectionKey READ connectionKey NOTIFY serviceStatesChanged)
    /*! Network detail (phase 6 §3.2): members/labels/options of the selected network. */
    Q_PROPERTY(Kontainer::NetworkDetailController *networkDetail READ networkDetail CONSTANT)

    /* Refresh state (§15/§16) */
    Q_PROPERTY(QDateTime lastUpdated READ lastUpdated NOTIFY refreshStateChanged)
    Q_PROPERTY(bool updateFailed READ updateFailed NOTIFY refreshStateChanged)
    Q_PROPERTY(bool stale READ stale NOTIFY refreshStateChanged)

    Q_PROPERTY(Kontainer::EngineStatus *engine READ engine CONSTANT)
    Q_PROPERTY(Kontainer::StorageStatus *storage READ storage CONSTANT)
    Q_PROPERTY(Kontainer::ContainerModel *containers READ containers CONSTANT)
    Q_PROPERTY(Kontainer::ImageModel *images READ images CONSTANT)
    /*! Search/filter/sorted list used by the ListView (§9/§32). */
    Q_PROPERTY(Kontainer::ContainerFilterModel *containerList READ containerList CONSTANT)
    Q_PROPERTY(Kontainer::ImageFilterModel *imageList READ imageList CONSTANT)
    Q_PROPERTY(Kontainer::ContainerDetailController *containerDetail READ containerDetail CONSTANT)
    Q_PROPERTY(Kontainer::ImageDetailController *imageDetail READ imageDetail CONSTANT)
    /*! Write-operation orchestration and result channel (ARCH_V4 §2.2.4). */
    Q_PROPERTY(Kontainer::OperationController *operations READ operations CONSTANT)
    /*! User-level runtime config (rootless: ~/.config/docker/daemon.json), no privileges needed. */
    Q_PROPERTY(Kontainer::DaemonConfigController *daemonConfigUser READ daemonConfigUser CONSTANT)
    /*! System-level runtime config (/etc/docker/daemon.json), protected area. */
    Q_PROPERTY(Kontainer::DaemonConfigController *daemonConfigSystem READ daemonConfigSystem CONSTANT)
    /*! Registry auth (KWallet credentials + /auth check + CLI import, ARCH_V5_V8 §2.6/§2.7). */
    Q_PROPERTY(Kontainer::RegistryAuthController *registryAuth READ registryAuth CONSTANT)
    /*! Last "open host path" failure; empty means none. */
    Q_PROPERTY(QString hostPathError READ hostPathError NOTIFY hostPathErrorChanged)

public:
    /*! Whole-page state: Idle / Loading / Ready / Error (§14). */
    enum class State {
        Idle,
        Loading,
        Ready,
        Error,
    };
    Q_ENUM(State)

    /*! Engine card state (§14: the model provides explicit states; QML never assembles them). */
    enum class EngineState {
        Loading, /*!< first load, no result yet */
        Ready, /*!< connected and /info succeeded, counts are trustworthy */
        Partial, /*!< connected but /info summary failed: version available, counts not */
        Refreshing, /*!< connected, refreshing */
        Unavailable, /*!< not connected */
    };
    Q_ENUM(EngineState)

    /*! List / section dataset state. */
    enum class ListState {
        Idle, /*!< not requested yet */
        Loading, /*!< first load in progress */
        Ready, /*!< has data */
        Empty, /*!< request succeeded but returned no entries (not an error) */
        Error, /*!< request failed */
    };
    Q_ENUM(ListState)

    /*! The caller owns the backend's lifetime: this object only holds a pointer. */
    /*!
     * `hostPaths` is injected by the composition root (KioHostPathService in production, a fake in
     * tests); when null, container mount rows offer no "open host directory" action.
     */
    /*!
     * A null `credentialBackend` means KWallet (the production path).
     *
     * The injection point serves tests and offscreen rendering: KWallet pops an unlock dialog and
     * writes to the user's real wallet, which is nondeterministic and must not happen in automation.
     */
    explicit StatusController(DockerBackendInterface *backend,
                              HostPathService *hostPaths = nullptr,
                              QObject *parent = nullptr,
                              CredentialBackend *credentialBackend = nullptr,
                              MountPresetStore *mountPresetStore = nullptr,
                              DirectoryPicker *directoryPicker = nullptr,
                              ServiceStatusBackend *serviceStatus = nullptr);
    ~StatusController() override;

    State state() const
    {
        return m_state;
    }
    EngineState engineState() const
    {
        return m_engineState;
    }
    ListState containersState() const
    {
        return m_containersState;
    }
    ListState imagesState() const
    {
        return m_imagesState;
    }
    ListState storageState() const
    {
        return m_storageState;
    }
    ListState networksState() const
    {
        return m_networksState;
    }
    ListState volumesState() const
    {
        return m_volumesState;
    }
    QString stateKey() const;
    QString engineStateKey() const;
    QString containersStateKey() const;
    QString imagesStateKey() const;
    QString storageStateKey() const;
    QString networksStateKey() const;
    QString volumesStateKey() const;
    /*!
     * Semantic key of the engine state (positive / neutral / negative / disabled).
     *
     * Partial means "connected, but /info is unreadable" — a degradation rather than a failure, so
     * it is neutral (warning), not negative (error).
     */
    QString engineStateSemanticKey() const;
    /*! Icon name of the engine state (icon theme name). */
    QString engineStateIconName() const;

    bool busy() const
    {
        return m_busy;
    }
    QString engineError() const
    {
        return m_engineError;
    }
    QString containersError() const
    {
        return m_containersError;
    }
    QString imagesError() const
    {
        return m_imagesError;
    }
    QString storageError() const
    {
        return m_storageError;
    }
    QString networksError() const
    {
        return m_networksError;
    }
    QString volumesError() const
    {
        return m_volumesError;
    }
    QString endpoint() const;
    /*! e.g. "0.3.0+1e3b56e (2026-09-17 08:50 UTC)". */
    QString buildStamp() const;

    bool autoRefreshEnabled() const;
    void setAutoRefreshEnabled(bool enabled);
    int autoRefreshInterval() const;
    int storageRefreshInterval() const;

    QDateTime lastUpdated() const;
    bool updateFailed() const;
    bool stale() const;

    EngineStatus *engine() const
    {
        return m_engine;
    }
    StorageStatus *storage() const
    {
        return m_storage;
    }
    ContainerModel *containers() const
    {
        return m_containerModel;
    }
    ImageModel *images() const
    {
        return m_imageModel;
    }
    ContainerFilterModel *containerList() const
    {
        return m_containerFilter;
    }
    ImageFilterModel *imageList() const
    {
        return m_imageFilter;
    }
    NetworkModel *networkModel() const
    {
        return m_networkModel;
    }
    HostPortModel *portModel() const
    {
        return m_hostPortModel;
    }
    HostPortFilterModel *hostPortList() const
    {
        return m_hostPortFilter;
    }
    int declaredNotPublishedCount() const;
    int startTabFromEnvironment() const;
    int reservedPortCount() const;
    int inUsePortCount() const;
    QVariantList portRanges() const;
    /*! Invalidate the cached range map (call on data/filter change; recomputed on next read). */
    void invalidatePortRanges();
    int nextFreeHostPort() const;
    NetworkFilterModel *networkList() const
    {
        return m_networkFilter;
    }
    NetworkDetailController *networkDetail() const
    {
        return m_networkDetail;
    }
    VolumeModel *volumeModel() const
    {
        return m_volumeModel;
    }
    VolumeFilterModel *volumeList() const
    {
        return m_volumeFilter;
    }
    VolumeDetailController *volumeDetail() const
    {
        return m_volumeDetail;
    }
    CreateContainerController *createContainer() const
    {
        return m_createContainer;
    }
    MountPresetStore *mountPresets() const
    {
        return m_mountPresets;
    }
    DirectoryPicker *directoryPicker() const
    {
        return m_directoryPicker;
    }
    ServiceStatusBackend *services() const
    {
        return m_services;
    }
    CommandHistoryStore *commandHistory() const
    {
        return m_commandHistory;
    }
    /*! Refined connection state (see connectionKey). */
    QString connectionKey() const;
    /*! Service states changed (the connection key may follow). */
    Q_SIGNAL void serviceStatesChanged();
    ContainerDetailController *containerDetail() const
    {
        return m_containerDetail;
    }
    ImageDetailController *imageDetail() const
    {
        return m_imageDetail;
    }
    OperationController *operations() const
    {
        return m_operations;
    }
    DaemonConfigController *daemonConfigUser() const
    {
        return m_daemonConfigUser;
    }
    DaemonConfigController *daemonConfigSystem() const
    {
        return m_daemonConfigSystem;
    }
    RegistryAuthController *registryAuth() const
    {
        return m_registryAuth;
    }
    QString hostPathError() const
    {
        return m_hostPathError;
    }
    RefreshScheduler *scheduler() const
    {
        return m_scheduler;
    }

    DockerBackendInterface *backend() const
    {
        return m_backend;
    }

    /* --- Queries shared by forms and presets (ARCH_V5_V8 §1.6: no rule duplication in QML) --- */

    /*! Host path state key: directory / missing / notADirectory / notApplicable. */
    Q_INVOKABLE QString hostPathStateKey(const QString &path) const;
    /*! Open a host directory in the file manager (returns accepted; failures via hostPathError). */
    Q_INVOKABLE bool openHostPath(const QString &path);

    /*!
     * All currently published host port bindings, e.g. `0.0.0.0:8080`.
     * The create form pre-checks port conflicts with them (logic in Presentation.hostPortConflicts).
     */
    Q_INVOKABLE QStringList portBindingsInUse() const;
    /*! Number of running containers (impact hint for daemon restarts). */
    Q_INVOKABLE int runningContainerCount() const;

public Q_SLOTS:
    /*! Manual refresh (required by §16); the backend deduplicates requests (§29). */
    void refresh();
    /*!
     * Retry only the storage dataset (partial-failure recovery path, §30/§55).
     * QML may not touch the backend directly (§4/§43), hence this explicit entry point.
     */
    void retryStorage();
    /*! The network list is low-frequency: refreshed when entering the networks page (phase 6 §3.2). */
    /*! Refresh the network list (called proactively, e.g. after creating a container). */
    /*!
     * Watchdog interval in milliseconds. For tests: defaults to `RefreshPolicy::kInFlightWatchdog`
     * and must stay tunable, otherwise cases would have to wait 20 seconds.
     */
    Q_INVOKABLE void setInFlightWatchdogMs(int milliseconds);
    /*!
     * Trigger one **automatic** refresh (the same path the timer takes).
     *
     * For tests: a manual refresh resets the failure counter (B2), so assertions such as
     * "consecutive failures → stale" are only reachable through the automatic path.
     */
    Q_INVOKABLE void requestAutomaticRefreshForTesting();
    Q_INVOKABLE int inFlightWatchdogMs() const;

    /*!
     * Read the low-frequency lists (networks, volumes) once when the page is first entered.
     *
     * Their counts appear in tab titles, so they cannot wait for a click (report: the count stayed
     * 0 until the tab was opened, then became 4). Idempotent: read once, afterwards still driven by
     * tab switches and changes.
     */
    void loadLowFrequencyListsOnce();

    Q_INVOKABLE void refreshNetworks();
    /*!
     * Called when the ports page opens: refresh containers and inspect each **running** container
     * (only to learn which host ports they declare, see decision 3 in `ARCH_next_ports.md`).
     */
    Q_INVOKABLE void refreshPorts();
    /*! The single place that rebuilds the port table, the counts and the range map. */
    void rebuildPorts();
    /*! Volume list is low-frequency too (phase 6 §3.5); `includeUsage=false` skips usage scanning. */
    void refreshVolumes(bool includeUsage = true);

Q_SIGNALS:
    void stateChanged();
    /*! The "declared but not published" count changed (the ports-page banner follows it). */
    void declaredNotPublishedCountChanged();
    /*! Port table / range map data changed. */
    void portRangesChanged();
    void engineStateChanged();
    void containersStateChanged();
    void imagesStateChanged();
    void storageStateChanged();
    void networksStateChanged();
    void networksErrorChanged();
    void volumesStateChanged();
    void volumesErrorChanged();
    void busyChanged();
    void engineErrorChanged();
    void containersErrorChanged();
    void imagesErrorChanged();
    void storageErrorChanged();
    void autoRefreshEnabledChanged();
    /*! The "open host path" failure hint changed. */
    void hostPathErrorChanged();
    void refreshStateChanged();

private:
    /*! Failure hint for host path actions (shared by forms and the mounts section). */
    void setHostPathError(const QString &text);
    void onEngineUpdated();
    void onContainersUpdated();
    void onImagesUpdated();
    void onNetworksUpdated();
    void onVolumesUpdated();
    void onStorageUpdated();
    void onLoadingChanged();
    /*! Watchdog fired: abandon in-flight requests and report a timeout (fallback for B3/B4). */
    void onBusyWatchdogTimeout();
    void onSectionFailed(DockerBackendInterface::Section section, const DockerError &error);
    void updateStates();
    void setSectionError(DockerBackendInterface::Section section, const QString &text);
    EngineState computeEngineState() const;
    ListState computeListState(bool ok, bool failed, int count) const;
    ListState computeStorageState() const;
    static QString stateKeyFor(State state);
    static QString engineStateKeyFor(EngineState state);
    static QString listStateKeyFor(ListState state);

    DockerBackendInterface *m_backend = nullptr;
    RefreshScheduler *m_scheduler = nullptr;
    EngineStatus *m_engine = nullptr;
    StorageStatus *m_storage = nullptr;
    ContainerModel *m_containerModel = nullptr;
    ImageModel *m_imageModel = nullptr;
    VolumeModel *m_volumeModel = nullptr;
    VolumeFilterModel *m_volumeFilter = nullptr;
    VolumeDetailController *m_volumeDetail = nullptr;
    ListState m_volumesState = ListState::Idle;
    QString m_volumesError;
    bool m_volumesOk = false;
    bool m_volumesFailed = false;
    NetworkModel *m_networkModel = nullptr;
    /*!
     * Host bindings **declared** by each container (inspect's `HostConfig.PortBindings`).
     *
     * Taken only for **running** containers and only once when the ports page opens — a single view
     * is not worth inspecting every container.
     */
    /*
     * Range map cache.
     *
     * `portRanges()` is the data source QML binds to; one recompute walks the whole filtered model,
     * clusters, then looks up every tile's state. Caching it and recomputing only when data or
     * filters really change avoids that work when switching filters (users saw 1-2 s freezes).
     */
    mutable QVariantList m_portRanges;
    mutable bool m_portRangesDirty = true;

    QHash<QString, QList<DeclaredPortBinding>> m_declaredPorts;
    /*! Containers already inspected this round (avoids re-inspecting on every refresh). */
    QStringList m_declaredRequested;
    NetworkFilterModel *m_networkFilter = nullptr;
    NetworkDetailController *m_networkDetail = nullptr;
    HostPortModel *m_hostPortModel = nullptr;
    HostPortFilterModel *m_hostPortFilter = nullptr;
    ListState m_networksState = ListState::Idle;
    QString m_networksError;
    bool m_networksOk = false;
    bool m_networksFailed = false;
    ContainerFilterModel *m_containerFilter = nullptr;
    ImageFilterModel *m_imageFilter = nullptr;
    ContainerDetailController *m_containerDetail = nullptr;
    ImageDetailController *m_imageDetail = nullptr;
    OperationController *m_operations = nullptr;
    /* These two depend on m_operations and **must** be declared after it: members initialize in
       declaration order, so declaring them earlier hands the wizard an unconstructed
       OperationController. */
    MountPresetStore *m_mountPresets = nullptr;
    /*! Command history (tool data → ~/.config/kcm_dockerrc); the wizard needs it, so it comes first. */
    CommandHistoryStore *m_commandHistory = nullptr;
    CreateContainerController *m_createContainer = nullptr;
    /*! Directory picker: native dialog by default; tests/rendering inject a stub. */
    DirectoryPicker *m_directoryPicker = nullptr;
    /*!
     * In-flight watchdog: abandon requests when busy lasts too long (reports B3/B4: a permanent
     * "loading / backend busy" state).
     */
    QTimer *m_busyWatchdog = nullptr;
    /*! Whether the low-frequency lists (networks/volumes) were already read once. */
    bool m_lowFrequencyLoaded = false;
    /*! Service state source (read-only systemd D-Bus by default; tests inject a stub). */
    ServiceStatusBackend *m_services = nullptr;
    HostPathService *m_hostPaths = nullptr;
    DaemonConfigController *m_daemonConfigUser = nullptr;
    DaemonConfigController *m_daemonConfigSystem = nullptr;
    /*! Credential backend (KWallet): constructed once in the core, shared by store and controller. */
    CredentialBackend *m_credentialBackend = nullptr;
    CredentialStore *m_credentialStore = nullptr;
    RegistryAuthController *m_registryAuth = nullptr;
    QString m_hostPathError;

    State m_state = State::Idle;
    EngineState m_engineState = EngineState::Loading;
    ListState m_containersState = ListState::Idle;
    ListState m_imagesState = ListState::Idle;
    ListState m_storageState = ListState::Idle;
    bool m_busy = false;
    /*! Reason the watchdog reports when abandoning a request (displayed; cleared on next success). */
    QString m_timeoutReason;


    QString m_engineError;
    QString m_containersError;
    QString m_imagesError;
    QString m_storageError;

    bool m_engineOk = false;
    bool m_containersOk = false;
    bool m_imagesOk = false;
    bool m_storageOk = false;
    bool m_engineFailed = false;
    bool m_containersFailed = false;
    bool m_imagesFailed = false;
    bool m_storageFailed = false;
    bool m_refreshRequested = false;
};

} // namespace Kontainer
