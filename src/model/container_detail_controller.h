/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/docker_backend_interface.h"
#include "domain/container_detail.h"
#include "backend/host_path_service.h"
#include "model/detail_list_model.h"
#include "model/mount_list_model.h"
#include "model/port_mapping_group_model.h"
#include "model/port_mapping_model.h"
#include "model/container_log_controller.h"
#include "model/metrics_model.h"

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QStringList>

class QTimer;

namespace Kontainer
{

/*!
 * Container Detail 的 controller（ARCH_V2 §7/§27/§28/§31/§43）。
 *
 * 生命周期由页面驱动：页面进入调用 start()，离开调用 stop()。
 *  - start(): 请求 inspect + 启动资源采样（仅对 running/paused 容器）+ 低频复核静态信息
 *  - stop():  停止低频复核 + 停止 stats 采样 + 释放 metrics 历史（§27）
 *
 * 不负责：HTTP、JSON、Docker 语义解释、UI 布局。
 */
class ContainerDetailController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString containerId READ containerId WRITE setContainerId NOTIFY containerIdChanged)
    Q_PROPERTY(QString loadStateKey READ loadStateKey NOTIFY stateChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY stateChanged)
    Q_PROPERTY(bool hasDetail READ hasDetail NOTIFY changed)
    Q_PROPERTY(bool running READ running NOTIFY changed)

    /* Overview（一级信息，§7.2） */
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(QString shortId READ shortId NOTIFY changed)
    Q_PROPERTY(QString image READ image NOTIFY changed)
    Q_PROPERTY(QString stateKey READ stateKey NOTIFY changed)
    Q_PROPERTY(QString stateText READ stateText NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString healthKey READ healthKey NOTIFY changed)
    Q_PROPERTY(QString healthText READ healthText NOTIFY changed)
    Q_PROPERTY(QDateTime created READ created NOTIFY changed)
    Q_PROPERTY(QDateTime started READ started NOTIFY changed)
    Q_PROPERTY(QDateTime finished READ finished NOTIFY changed)
    Q_PROPERTY(QString platform READ platform NOTIFY changed)

    /* Runtime */
    Q_PROPERTY(int exitCode READ exitCode NOTIFY changed)
    Q_PROPERTY(bool oomKilled READ oomKilled NOTIFY changed)
    Q_PROPERTY(int restartCount READ restartCount NOTIFY changed)
    Q_PROPERTY(int pid READ pid NOTIFY changed)
    Q_PROPERTY(QString restartPolicy READ restartPolicy NOTIFY changed)

    /* Configuration（三级信息，默认折叠） */
    Q_PROPERTY(QStringList command READ command NOTIFY changed)
    Q_PROPERTY(QStringList entrypoint READ entrypoint NOTIFY changed)
    Q_PROPERTY(QString workingDirectory READ workingDirectory NOTIFY changed)
    Q_PROPERTY(QString user READ user NOTIFY changed)
    Q_PROPERTY(QString hostname READ hostname NOTIFY changed)
    Q_PROPERTY(int environmentCount READ environmentCount NOTIFY changed)
    /*! 环境变量的真实值（§40：只有用户显式展开时才应由 QML 读取渲染）。 */
    Q_PROPERTY(QStringList environment READ environment NOTIFY changed)

    /* 结构化子列表 */
    /*! 已发布的端口映射：端口拓扑的数据源（ARCH_V4 §2.1.2）。 */
    Q_PROPERTY(Kontainer::PortMappingModel *publishedPorts READ publishedPorts CONSTANT)
    /*! 只 EXPOSE、没有映射到宿主的端口。 */
    Q_PROPERTY(Kontainer::PortMappingModel *unpublishedPorts READ unpublishedPorts CONSTANT)
    /*!
     * 按容器端口分组后的已发布映射（拓扑图用）。
     *
     * 同一个容器端口映射到多个宿主地址时，左列只出现一次、右侧用分支连出去
     * （ARCH_V5_V8 §2.1 拓扑形态修订）。
     */
    Q_PROPERTY(Kontainer::PortMappingGroupModel *portGroups READ portGroups CONSTANT)
    Q_PROPERTY(Kontainer::DetailListModel *networks READ networks CONSTANT)
    Q_PROPERTY(Kontainer::MountListModel *mounts READ mounts CONSTANT)
    /*! 最近一次「打开宿主目录」的失败说明；为空表示没有失败。 */
    Q_PROPERTY(QString mountActionError READ mountActionError NOTIFY mountActionErrorChanged)
    Q_PROPERTY(Kontainer::DetailListModel *labels READ labels CONSTANT)
    Q_PROPERTY(Kontainer::DetailListModel *environmentVariables READ environmentVariables CONSTANT)

    /* 资源（§22） */
    Q_PROPERTY(Kontainer::MetricsModel *metrics READ metrics CONSTANT)
    /*! 日志控制台（§3.1）：文本、状态与暂停/清空都在它身上。 */
    Q_PROPERTY(Kontainer::ContainerLogController *logs READ logs CONSTANT)

public:
    /*!
     * `hostPaths` 由组合根（DockerKcm）注入；测试传 Fake，因此单测不会真的弹出文件管理器。
     * 允许为空：为空时挂载行不提供打开动作。
     */
    explicit ContainerDetailController(DockerBackendInterface *backend, HostPathService *hostPaths = nullptr, QObject *parent = nullptr);
    ~ContainerDetailController() override;

    void setContainerId(const QString &id);
    QString containerId() const
    {
        return m_containerId;
    }

    QString loadStateKey() const
    {
        return m_loadStateKey;
    }
    QString errorText() const
    {
        return m_errorText;
    }
    bool hasDetail() const
    {
        return m_detail.isValid();
    }
    bool running() const
    {
        return m_detail.state == ContainerState::Running || m_detail.state == ContainerState::Paused;
    }

    QString name() const
    {
        return m_detail.name;
    }
    QString shortId() const;
    QString image() const
    {
        return m_detail.image;
    }
    QString stateKey() const;
    QString stateText() const;
    QString status() const
    {
        return m_detail.status;
    }
    QString healthKey() const;
    QString healthText() const;
    QDateTime created() const
    {
        return m_detail.created;
    }
    QDateTime started() const
    {
        return m_detail.started;
    }
    QDateTime finished() const
    {
        return m_detail.finished;
    }
    QString platform() const
    {
        return m_detail.platform;
    }

    int exitCode() const
    {
        return m_detail.exitCode;
    }
    bool oomKilled() const
    {
        return m_detail.oomKilled;
    }
    int restartCount() const
    {
        return m_detail.restartCount;
    }
    int pid() const
    {
        return m_detail.pid;
    }
    QString restartPolicy() const
    {
        return m_detail.restartPolicy;
    }

    QStringList command() const
    {
        return m_detail.command;
    }
    QStringList entrypoint() const
    {
        return m_detail.entrypoint;
    }
    QString workingDirectory() const
    {
        return m_detail.workingDirectory;
    }
    QString user() const
    {
        return m_detail.user;
    }
    QString hostname() const
    {
        return m_detail.hostname;
    }
    int environmentCount() const
    {
        return int(m_detail.environment.size());
    }
    QStringList environment() const
    {
        return m_detail.environment;
    }

    PortMappingModel *publishedPorts() const
    {
        return m_publishedPorts;
    }
    PortMappingModel *unpublishedPorts() const
    {
        return m_unpublishedPorts;
    }
    PortMappingGroupModel *portGroups() const
    {
        return m_portGroups;
    }
    DetailListModel *networks() const
    {
        return m_networks;
    }
    MountListModel *mounts() const
    {
        return m_mounts;
    }
    QString mountActionError() const
    {
        return m_mountActionError;
    }
    DetailListModel *labels() const
    {
        return m_labels;
    }
    DetailListModel *environmentVariables() const
    {
        return m_environment;
    }
    ContainerLogController *logs() const
    {
        return m_logs;
    }
    MetricsModel *metrics() const
    {
        return m_metrics;
    }

    /*! 在系统文件管理器中打开第 `row` 条挂载的宿主目录（ARCH_V4 §2.1.1）。 */
    Q_INVOKABLE void openMountHostPath(int row);
    /*! 清掉打开失败的提示。 */
    Q_INVOKABLE void dismissMountActionError();

public Q_SLOTS:
    /*! 页面进入（§27）。 */
    void start();
    /*! 页面离开（§27）。 */
    void stop();

    /*!
     * 进入 / 离开日志分区（ARCH_V5_V8 §3.1.4）。
     *
     * 进分区才连接、离开即断开：日志是长连接，不该在用户看别的分区时挂着。
     * `tty` 取自容器详情（`Config.Tty`），判错会把 8 字节帧头当成日志正文。
     */
    /*!
     * 容器当前连接的网络名（界面据此把已连接的网络标出来、不给重复连接）。
     *
     * 名字来自 inspect 的 `NetworkSettings.Networks`（以名字为键）；
     * 连接/断开用的网络 Id 由 `NetworkModel::idForName()` 转换。
     */
    Q_INVOKABLE QStringList connectedNetworkNames() const;

    Q_INVOKABLE void startLogs();
    Q_INVOKABLE void stopLogs();
    /*! 详情加载失败后的重试（§31）。 */
    void refresh();
    /*!
     * 静默重读（ARCH_V4 §2.2.4「写后即读」）。
     *
     * 写操作成功后调用：不把页面打回 loading（否则会闪一下「正在加载」），
     * 只重新 inspect 一次，让状态徽标、资源分区与统计采样跟着切换。
     */
    void reload();

Q_SIGNALS:
    void containerIdChanged();
    void mountActionErrorChanged();
    void stateChanged();
    void changed();

private:
    void onDetailUpdated();
    void onSectionFailed(DockerBackendInterface::Section section, const DockerError &error);
    void onStatsUpdated();
    void setLoadState(const QString &stateKey, const QString &errorText = QString());
    void rebuildLists();
    /*! 挂载行：把 domain 挂载 + 宿主路径探测结果合成 presentation 条目。 */
    QList<MountEntry> mountEntries() const;
    /*! 端口行：已发布 / 未发布两组，排序稳定。 */
    void rebuildPorts();
    void setMountActionError(const QString &text);

    DockerBackendInterface *m_backend = nullptr;
    HostPathService *m_hostPaths = nullptr;
    QTimer *m_reinspectTimer = nullptr;
    MetricsModel *m_metrics = nullptr;
    ContainerLogController *m_logs = nullptr;
    PortMappingModel *m_publishedPorts = nullptr;
    PortMappingGroupModel *m_portGroups = nullptr;
    PortMappingModel *m_unpublishedPorts = nullptr;
    DetailListModel *m_networks = nullptr;
    MountListModel *m_mounts = nullptr;
    QString m_mountActionError;
    DetailListModel *m_labels = nullptr;
    DetailListModel *m_environment = nullptr;

    QString m_containerId;
    QString m_loadStateKey = QStringLiteral("idle");
    QString m_errorText;
    ContainerDetail m_detail;
    bool m_started = false;
};

} // namespace Kontainer
