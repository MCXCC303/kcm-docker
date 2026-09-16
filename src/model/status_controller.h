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
#include "model/image_model.h"
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

    /*! 一期 endpoint 不可在运行时改变（没有配置写入口），因此是 CONSTANT。 */
    Q_PROPERTY(QString endpoint READ endpoint CONSTANT)

    Q_PROPERTY(bool autoRefreshEnabled READ autoRefreshEnabled WRITE setAutoRefreshEnabled NOTIFY autoRefreshEnabledChanged)
    Q_PROPERTY(int autoRefreshInterval READ autoRefreshInterval CONSTANT)
    Q_PROPERTY(int storageRefreshInterval READ storageRefreshInterval CONSTANT)

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
    explicit StatusController(DockerBackendInterface *backend, QObject *parent = nullptr);
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
    QString stateKey() const;
    QString engineStateKey() const;
    QString containersStateKey() const;
    QString imagesStateKey() const;
    QString storageStateKey() const;
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
    QString endpoint() const;

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
    ContainerDetailController *containerDetail() const
    {
        return m_containerDetail;
    }
    ImageDetailController *imageDetail() const
    {
        return m_imageDetail;
    }
    RefreshScheduler *scheduler() const
    {
        return m_scheduler;
    }

    DockerBackendInterface *backend() const
    {
        return m_backend;
    }

public Q_SLOTS:
    /*! 手动刷新（§16 必须项）；请求去重由 backend 负责（§29）。 */
    void refresh();
    /*!
     * 只重试 storage 数据集（§30/§55 的部分失败恢复路径）。
     * QML 不允许直接访问 backend（§4/§43），因此提供这个显式入口。
     */
    void retryStorage();

Q_SIGNALS:
    void stateChanged();
    void engineStateChanged();
    void containersStateChanged();
    void imagesStateChanged();
    void storageStateChanged();
    void busyChanged();
    void engineErrorChanged();
    void containersErrorChanged();
    void imagesErrorChanged();
    void storageErrorChanged();
    void autoRefreshEnabledChanged();
    void refreshStateChanged();

private:
    void onEngineUpdated();
    void onContainersUpdated();
    void onImagesUpdated();
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
    ContainerFilterModel *m_containerFilter = nullptr;
    ImageFilterModel *m_imageFilter = nullptr;
    ContainerDetailController *m_containerDetail = nullptr;
    ImageDetailController *m_imageDetail = nullptr;

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
