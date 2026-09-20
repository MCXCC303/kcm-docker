/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
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
 * Controller for Container Detail (ARCH_V2 §7/§27/§28/§31/§43).
 *
 * The page drives its lifecycle: start() on entry, stop() on exit.
 *  - start(): request inspect + start resource sampling (running/paused containers only) + low-frequency
 *    re-check of static info
 *  - stop():  stop the re-check + stop stats sampling + drop metrics history (§27)
 *
 * Not responsible for: HTTP, JSON, Docker semantics, UI layout.
 */
class ContainerDetailController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString containerId READ containerId WRITE setContainerId NOTIFY containerIdChanged)
    Q_PROPERTY(QString loadStateKey READ loadStateKey NOTIFY stateChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY stateChanged)
    Q_PROPERTY(bool hasDetail READ hasDetail NOTIFY changed)
    Q_PROPERTY(bool running READ running NOTIFY changed)

    /* Overview (primary info, §7.2) */
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(QString shortId READ shortId NOTIFY changed)
    Q_PROPERTY(QString image READ image NOTIFY changed)
    /*!
     * Full image ID (`sha256:…`).
     *
     * The detail page's image row can open the image detail with this ID (empty means the engine did not
     * report one, and that row is not clickable).
     */
    Q_PROPERTY(QString imageId READ imageId NOTIFY changed)
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

    /* Configuration (tertiary info, collapsed by default) */
    Q_PROPERTY(QStringList command READ command NOTIFY changed)
    Q_PROPERTY(QStringList entrypoint READ entrypoint NOTIFY changed)
    Q_PROPERTY(QString workingDirectory READ workingDirectory NOTIFY changed)
    Q_PROPERTY(QString user READ user NOTIFY changed)
    Q_PROPERTY(QString hostname READ hostname NOTIFY changed)
    Q_PROPERTY(int environmentCount READ environmentCount NOTIFY changed)
    /*! Actual environment values (§40: QML should read them only when the user expands the section). */
    Q_PROPERTY(QStringList environment READ environment NOTIFY changed)

    /* Structured sub-lists */
    /*! Published port mappings: the data source for the port topology (ARCH_V4 §2.1.2). */
    Q_PROPERTY(Kontainer::PortMappingModel *publishedPorts READ publishedPorts CONSTANT)
    /*! Ports that are only EXPOSEd and never mapped to the host. */
    Q_PROPERTY(Kontainer::PortMappingModel *unpublishedPorts READ unpublishedPorts CONSTANT)
    /*!
     * Published mappings grouped by container port (for the topology view).
     *
     * When one container port maps to several host addresses, the left column shows it once and the right
     * side branches out (ARCH_V5_V8 §2.1 topology revision).
     */
    Q_PROPERTY(Kontainer::PortMappingGroupModel *portGroups READ portGroups CONSTANT)
    Q_PROPERTY(Kontainer::DetailListModel *networks READ networks CONSTANT)
    Q_PROPERTY(Kontainer::MountListModel *mounts READ mounts CONSTANT)
    /*! Message from the last failed "open host directory"; empty means no failure. */
    Q_PROPERTY(QString mountActionError READ mountActionError NOTIFY mountActionErrorChanged)
    Q_PROPERTY(Kontainer::DetailListModel *labels READ labels CONSTANT)
    Q_PROPERTY(Kontainer::DetailListModel *environmentVariables READ environmentVariables CONSTANT)

    /* Resources (§22) */
    Q_PROPERTY(Kontainer::MetricsModel *metrics READ metrics CONSTANT)
    /*! Log console (§3.1): text, state, pause and clear all live on it. */
    Q_PROPERTY(Kontainer::ContainerLogController *logs READ logs CONSTANT)

    /*!
     * Names of the connected networks (a **property**, not a function).
     *
     * Reported after disconnecting a network: the "connect network" panel still marked it connected and
     * kept the button disabled. Cause: only `Q_INVOKABLE` existed, and in QML `isConnectable(name)` is a
     * **function call**, which builds no dependency, so bindings never re-evaluated (fifth time here).
     */
    Q_PROPERTY(QStringList connectedNetworkNames READ connectedNetworkNames NOTIFY changed)

public:
    /*!
     * `hostPaths` is injected by the composition root (DockerKcm); tests pass a Fake so unit tests never
     * launch a real file manager. May be null: mount rows then offer no open action.
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
    QString imageId() const
    {
        return m_detail.imageId;
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

    /*! Open the host directory of mount `row` in the system file manager (ARCH_V4 §2.1.1). */
    Q_INVOKABLE void openMountHostPath(int row);
    /*! Clear the open-failure message. */
    Q_INVOKABLE void dismissMountActionError();

public Q_SLOTS:
    /*! Page entered (§27). */
    void start();
    /*! Page left (§27). */
    void stop();

    /*!
     * Enter / leave the logs section (ARCH_V5_V8 §3.1.4).
     *
     * Connect on entry, disconnect on exit: logs are a long-lived stream that should not stay open while
     * the user views another section. `tty` comes from container detail (`Config.Tty`); judging it wrong
     * renders the 8-byte frame header as log text.
     */
    /*!
     * Networks the container is currently attached to (the UI marks them and blocks re-connecting).
     *
     * Names come from inspect's `NetworkSettings.Networks` (keyed by name); the network Ids used for
     * connect/disconnect are resolved by `NetworkModel::idForName()`.
     */

    /*! As above (for C++/legacy call sites; QML should use the property of the same name). */
    QStringList connectedNetworkNames() const;

    Q_INVOKABLE void startLogs();
    Q_INVOKABLE void stopLogs();
    /*! Retry after a failed detail load (§31). */
    void refresh();
    /*!
     * Silent reload (ARCH_V4 §2.2.4 "read after write").
     *
     * Called after a successful write: it does not push the page back to loading (which would flash
     * "Loading"), it only runs inspect again so the state badge, resource section and sampling follow.
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
    /*! Mount rows: combine domain mounts and host path probe results into presentation entries. */
    QList<MountEntry> mountEntries() const;
    /*! Port rows: published / unpublished groups, with stable ordering. */
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
