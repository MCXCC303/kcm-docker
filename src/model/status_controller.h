/*
    SPDX-FileCopyrightText: 2026 kontainer developers
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
#include "backend/credential_store.h"
#include "model/daemon_config_controller.h"
#include "model/operation_controller.h"
#include "model/registry_auth_controller.h"
#include "model/image_model.h"
#include "model/network_filter_model.h"
#include "model/network_detail_controller.h"
#include "model/network_model.h"
#include "model/create_container_controller.h"
#include "model/mount_preset_store.h"
#include "model/volume_detail_controller.h"
#include "model/volume_filter_model.h"
#include "model/volume_model.h"
#include "model/refresh_scheduler.h"
#include "model/storage_status.h"

#include <QDateTime>
#include <QObject>
#include <QString>

namespace Kontainer
{

/*!
 * KCM 与 backend 之间的 presentation controller（ARCH_V1 §6.2/§14，ARCH_V2 §13–§16/§30/§43）。
 *
 * 职责：
 *  - 把 backend 的域数据搬进 QML 可绑定的模型（容器 / 镜像 / Engine / Storage / Detail）
 *  - 维护明确的 UI 状态机：整页 State + 每个分区显式状态（字符串 key），QML 只比较单一状态值
 *  - 分区错误隔离：Storage / Detail / Metrics 失败不会让整页进入 Error（§30）
 *  - Last Updated / Stale / 部分失败提示（§15/§16）
 *  - 刷新调度委托给 RefreshScheduler（§14）；controller 自己不再维护页面级 QTimer
 *
 * 不负责：Docker URL 拼接、JSON 解析、HTTP 状态码判断、socket 访问、页面导航（§43）。
 */
class StatusController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(Kontainer::StatusController::State state READ state NOTIFY stateChanged)
    Q_PROPERTY(Kontainer::StatusController::EngineState engineState READ engineState NOTIFY engineStateChanged)
    Q_PROPERTY(Kontainer::StatusController::ListState containersState READ containersState NOTIFY containersStateChanged)
    Q_PROPERTY(Kontainer::StatusController::ListState imagesState READ imagesState NOTIFY imagesStateChanged)
    Q_PROPERTY(Kontainer::StatusController::ListState storageState READ storageState NOTIFY storageStateChanged)
    /*! QML 只使用字符串状态 key：同一个类里多个 Q_ENUM 有同名成员时，Type.Loading 会解析到错误枚举。 */
    Q_PROPERTY(QString stateKey READ stateKey NOTIFY stateChanged)
    Q_PROPERTY(QString engineStateKey READ engineStateKey NOTIFY engineStateChanged)
    Q_PROPERTY(QString containersStateKey READ containersStateKey NOTIFY containersStateChanged)
    Q_PROPERTY(QString imagesStateKey READ imagesStateKey NOTIFY imagesStateChanged)
    Q_PROPERTY(QString storageStateKey READ storageStateKey NOTIFY storageStateChanged)
    Q_PROPERTY(QString networksStateKey READ networksStateKey NOTIFY networksStateChanged)
    Q_PROPERTY(QString volumesStateKey READ volumesStateKey NOTIFY volumesStateChanged)
    /*!
     * Engine 连接状态的语义与图标（ARCH_V3 §2.1：语义判断属于 model 层，
     * QML 只把语义 key 翻译成主题颜色，不再自己 switch 状态字符串）。
     */
    Q_PROPERTY(QString engineStateSemanticKey READ engineStateSemanticKey NOTIFY engineStateChanged)
    Q_PROPERTY(QString engineStateIconName READ engineStateIconName NOTIFY engineStateChanged)

    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString engineError READ engineError NOTIFY engineErrorChanged)
    Q_PROPERTY(QString containersError READ containersError NOTIFY containersErrorChanged)
    Q_PROPERTY(QString imagesError READ imagesError NOTIFY imagesErrorChanged)
    Q_PROPERTY(QString storageError READ storageError NOTIFY storageErrorChanged)
    Q_PROPERTY(QString networksError READ networksError NOTIFY networksErrorChanged)
    /*! 数据卷列表（六期 §3.5）：同样是低频数据，进页面时刷新。 */
    Q_PROPERTY(ListState volumesState READ volumesState NOTIFY volumesStateChanged)
    Q_PROPERTY(QString volumesError READ volumesError NOTIFY volumesErrorChanged)

    /*! 一期 endpoint 不可在运行时改变（没有配置写入口），因此是 CONSTANT。 */
    Q_PROPERTY(QString endpoint READ endpoint CONSTANT)
    /*!
     * 构建标记（版本 + git 短哈希 + 构建时间）。
     *
     * 排查真实会话问题时，「用户装的到底是哪一次构建」必须能一眼确认——
     * 曾经因为安装与重新链接只差 0.3 秒而无法判断崩溃对应哪份代码。
     */
    Q_PROPERTY(QString buildStamp READ buildStamp CONSTANT)

    Q_PROPERTY(bool autoRefreshEnabled READ autoRefreshEnabled WRITE setAutoRefreshEnabled NOTIFY autoRefreshEnabledChanged)
    Q_PROPERTY(int autoRefreshInterval READ autoRefreshInterval CONSTANT)
    Q_PROPERTY(int storageRefreshInterval READ storageRefreshInterval CONSTANT)

    /*! 网络列表与过滤代理（六期 §3.2）。 */
    Q_PROPERTY(Kontainer::NetworkModel *networkModel READ networkModel CONSTANT)
    Q_PROPERTY(Kontainer::NetworkFilterModel *networkList READ networkList CONSTANT)
    /*! 数据卷列表与过滤代理。 */
    Q_PROPERTY(Kontainer::VolumeModel *volumeModel READ volumeModel CONSTANT)
    Q_PROPERTY(Kontainer::VolumeFilterModel *volumeList READ volumeList CONSTANT)
    /*! 数据卷详情（选中一个卷后读它的标签与驱动选项）。 */
    Q_PROPERTY(Kontainer::VolumeDetailController *volumeDetail READ volumeDetail CONSTANT)
    /*! 创建容器向导的状态与校验（七期 §4.4）。 */
    Q_PROPERTY(Kontainer::CreateContainerController *createContainer READ createContainer CONSTANT)
    /*! 挂载预设（七期 §4.1）：界面上可增删改与"从容器保存"。 */
    Q_PROPERTY(Kontainer::MountPresetStore *mountPresets READ mountPresets CONSTANT)
    /*! 网络详情（六期 §3.2）：选中一个网络后读它的成员/标签/选项。 */
    Q_PROPERTY(Kontainer::NetworkDetailController *networkDetail READ networkDetail CONSTANT)

    /* 刷新状态（§15/§16） */
    Q_PROPERTY(QDateTime lastUpdated READ lastUpdated NOTIFY refreshStateChanged)
    Q_PROPERTY(bool updateFailed READ updateFailed NOTIFY refreshStateChanged)
    Q_PROPERTY(bool stale READ stale NOTIFY refreshStateChanged)

    Q_PROPERTY(Kontainer::EngineStatus *engine READ engine CONSTANT)
    Q_PROPERTY(Kontainer::StorageStatus *storage READ storage CONSTANT)
    Q_PROPERTY(Kontainer::ContainerModel *containers READ containers CONSTANT)
    Q_PROPERTY(Kontainer::ImageModel *images READ images CONSTANT)
    /*! 经过搜索/过滤/排序的列表，供 ListView 使用（§9/§32）。 */
    Q_PROPERTY(Kontainer::ContainerFilterModel *containerList READ containerList CONSTANT)
    Q_PROPERTY(Kontainer::ImageFilterModel *imageList READ imageList CONSTANT)
    Q_PROPERTY(Kontainer::ContainerDetailController *containerDetail READ containerDetail CONSTANT)
    Q_PROPERTY(Kontainer::ImageDetailController *imageDetail READ imageDetail CONSTANT)
    /*! 写操作编排与结果通道（ARCH_V4 §2.2.4）。 */
    Q_PROPERTY(Kontainer::OperationController *operations READ operations CONSTANT)
    /*! 用户级运行时配置（rootless：~/.config/docker/daemon.json），不需要提权。 */
    Q_PROPERTY(Kontainer::DaemonConfigController *daemonConfigUser READ daemonConfigUser CONSTANT)
    /*! 系统级运行时配置（/etc/docker/daemon.json），受保护区。 */
    Q_PROPERTY(Kontainer::DaemonConfigController *daemonConfigSystem READ daemonConfigSystem CONSTANT)
    /*! 仓库认证（KWallet 凭据 + /auth 校验 + CLI 导入，ARCH_V5_V8 §2.6/§2.7）。 */
    Q_PROPERTY(Kontainer::RegistryAuthController *registryAuth READ registryAuth CONSTANT)
    /*! 最近一次「打开宿主路径」的失败说明；为空表示没有失败。 */
    Q_PROPERTY(QString hostPathError READ hostPathError NOTIFY hostPathErrorChanged)

public:
    /*! 整页状态：Idle / Loading / Ready / Error（§14）。 */
    enum class State {
        Idle,
        Loading,
        Ready,
        Error,
    };
    Q_ENUM(State)

    /*! Engine 卡片状态（§14：由 model 提供明确状态，QML 不自行拼装）。 */
    enum class EngineState {
        Loading, /*!< 首次加载中，还没有任何结果 */
        Ready, /*!< 已连接且 /info 成功，计数可信 */
        Partial, /*!< 已连接但汇总信息（/info）失败：版本可用、计数不可用 */
        Refreshing, /*!< 已连接，正在刷新 */
        Unavailable, /*!< 未连接 */
    };
    Q_ENUM(EngineState)

    /*! 列表 / 分区数据集状态。 */
    enum class ListState {
        Idle, /*!< 尚未请求 */
        Loading, /*!< 首次加载中 */
        Ready, /*!< 有数据 */
        Empty, /*!< 请求成功，但没有条目（不是错误） */
        Error, /*!< 请求失败 */
    };
    Q_ENUM(ListState)

    /*!
     * backend 的生命周期由调用方负责：本对象只持有指针，不接管所有权。
     */
    /*!
     * `hostPaths` 由组合根注入（生产是 KioHostPathService，测试是 Fake）；为空时
     * 容器详情的挂载行不提供「打开宿主目录」动作。
     */
    /*!
     * `credentialBackend` 为空时使用 KWallet（生产路径）。
     *
     * 注入点是给测试与离屏渲染用的：KWallet 会弹解锁框、写入用户真实钱包，
     * 自动化流程里既不确定也不该发生。
     */
    explicit StatusController(DockerBackendInterface *backend,
                              HostPathService *hostPaths = nullptr,
                              QObject *parent = nullptr,
                              CredentialBackend *credentialBackend = nullptr);
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
     * Engine 状态的语义 key（positive / neutral / negative / disabled）。
     *
     * 注意 Partial 是「已连接，但 /info 概要读不到」——属于降级而非失败，
     * 因此是 neutral（警告）而不是 negative（错误）。
     */
    QString engineStateSemanticKey() const;
    /*! Engine 状态的图标名（icon theme name）。 */
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
    /*! 形如 "0.3.0+1e3b56e (2026-09-17 08:50 UTC)"。 */
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

    /* --- 表单与预设共用的查询（ARCH_V5_V8 §1.6：不在 QML 里重复实现规则） --- */

    /*! 宿主路径状态 key：directory / missing / notADirectory / notApplicable。 */
    Q_INVOKABLE QString hostPathStateKey(const QString &path) const;
    /*! 用系统文件管理器打开宿主目录；返回是否已受理（失败原因走 hostPathError）。 */
    Q_INVOKABLE bool openHostPath(const QString &path);

    /*!
     * 当前所有已发布的宿主端口绑定，形如 `0.0.0.0:8080`。
     * 创建表单用它做端口冲突的前置检测（判定逻辑在 Presentation.hostPortConflicts）。
     */
    Q_INVOKABLE QStringList portBindingsInUse() const;
    /*! 运行中的容器数（重启 daemon 的影响提示）。 */
    Q_INVOKABLE int runningContainerCount() const;

public Q_SLOTS:
    /*! 手动刷新（§16 必须项）；请求去重由 backend 负责（§29）。 */
    void refresh();
    /*!
     * 只重试 storage 数据集（§30/§55 的部分失败恢复路径）。
     * QML 不允许直接访问 backend（§4/§43），因此提供这个显式入口。
     */
    void retryStorage();
    /*! 网络列表是低频数据：只在进入网络页面时刷新（六期 §3.2）。 */
    void refreshNetworks();
    /*! 数据卷列表同样是低频数据（六期 §3.5）；`includeUsage=false` 时不扫占用。 */
    void refreshVolumes(bool includeUsage = true);

Q_SIGNALS:
    void stateChanged();
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
    /*! 「打开宿主路径」失败提示变化。 */
    void hostPathErrorChanged();
    void refreshStateChanged();

private:
    /*! 宿主路径动作的失败提示（表单与挂载分区共用）。 */
    void setHostPathError(const QString &text);
    void onEngineUpdated();
    void onContainersUpdated();
    void onImagesUpdated();
    void onNetworksUpdated();
    void onVolumesUpdated();
    void onStorageUpdated();
    void onLoadingChanged();
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
    MountPresetStore *m_mountPresets = nullptr;
    CreateContainerController *m_createContainer = nullptr;
    ListState m_volumesState = ListState::Idle;
    QString m_volumesError;
    bool m_volumesOk = false;
    bool m_volumesFailed = false;
    NetworkModel *m_networkModel = nullptr;
    NetworkFilterModel *m_networkFilter = nullptr;
    NetworkDetailController *m_networkDetail = nullptr;
    ListState m_networksState = ListState::Idle;
    QString m_networksError;
    bool m_networksOk = false;
    bool m_networksFailed = false;
    ContainerFilterModel *m_containerFilter = nullptr;
    ImageFilterModel *m_imageFilter = nullptr;
    ContainerDetailController *m_containerDetail = nullptr;
    ImageDetailController *m_imageDetail = nullptr;
    OperationController *m_operations = nullptr;
    HostPathService *m_hostPaths = nullptr;
    DaemonConfigController *m_daemonConfigUser = nullptr;
    DaemonConfigController *m_daemonConfigSystem = nullptr;
    /*! 凭据后端（KWallet）：只在 core 里构造一次，存储与控制器共用。 */
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
