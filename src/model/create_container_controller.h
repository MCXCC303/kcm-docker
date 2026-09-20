/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/docker_backend_interface.h"
#include "domain/container_create_request.h"
#include "model/mount_preset_store.h"

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

namespace Kontainer
{

class CommandHistoryStore;
class ContainerDetailController;
class OperationController;

/*!
 * State and validation for the create-container wizard (ARCH_V5_V8 §4.3/§4.4).
 *
 * Why form state lives in C++ and not QML: half of the phase-seven validation matrix must **check
 * backend data** (duplicate names, port conflicts, whether the image is local) and the other half is
 * pure format rules; only together can unit tests cover them fully. QML does two things only: write
 * input in, draw `summary()` out.
 *
 * Steps use **stable keys** (`image` / `basics` / `ports` / `environment` / `mounts` / `resources` /
 * `summary`) instead of indices: step buttons, validation and tests all speak keys, so inserting or
 * reordering steps later cannot silently misalign (phase six hit this on tab indices).
 */
class CreateContainerController : public QObject
{
    Q_OBJECT

    /* ---------------- Steps ---------------- */
    Q_PROPERTY(QString stepKey READ stepKey NOTIFY stepChanged)
    Q_PROPERTY(int stepIndex READ stepIndex NOTIFY stepChanged)
    Q_PROPERTY(int stepCount READ stepCount CONSTANT)
    /*! Whether the current step can advance (`stepErrorKey()` gives the reason when it cannot). */
    Q_PROPERTY(bool canAdvance READ canAdvance NOTIFY changed)
    /*! Problem key of the current step (empty = no problem). */
    Q_PROPERTY(QString stepErrorKey READ stepErrorKey NOTIFY changed)
    /*! Whether we are on the last step (the review). */
    Q_PROPERTY(bool onSummary READ onSummary NOTIFY stepChanged)

    /* ---------------- Form fields ---------------- */
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY changed)
    Q_PROPERTY(QString image READ image WRITE setImage NOTIFY changed)
    Q_PROPERTY(QString commandText READ commandText WRITE setCommandText NOTIFY changed)
    Q_PROPERTY(QString entrypointText READ entrypointText WRITE setEntrypointText NOTIFY changed)
    Q_PROPERTY(QString workingDirectory READ workingDirectory WRITE setWorkingDirectory NOTIFY changed)
    Q_PROPERTY(QString user READ user WRITE setUser NOTIFY changed)
    Q_PROPERTY(QString hostname READ hostname WRITE setHostname NOTIFY changed)
    Q_PROPERTY(QString network READ network WRITE setNetwork NOTIFY changed)
    Q_PROPERTY(QString networkAliasesText READ networkAliasesText WRITE setNetworkAliasesText NOTIFY changed)
    Q_PROPERTY(QString restartPolicy READ restartPolicy WRITE setRestartPolicy NOTIFY changed)
    Q_PROPERTY(int restartMaxRetries READ restartMaxRetries WRITE setRestartMaxRetries NOTIFY changed)
    Q_PROPERTY(qint64 memoryLimitBytes READ memoryLimitBytes WRITE setMemoryLimitBytes NOTIFY changed)
    Q_PROPERTY(double cpus READ cpus WRITE setCpus NOTIFY changed)
    Q_PROPERTY(bool privileged READ privileged WRITE setPrivileged NOTIFY changed)
    /*! Interactive stdin (`-i`): **on by default**, or commands in images like alpine read EOF and exit. */
    Q_PROPERTY(bool openStdin READ openStdin WRITE setOpenStdin NOTIFY changed)
    /*! Allocate a pseudo-TTY (`-t`): on by default (with `-i` it is the usual manual debug combo). */
    Q_PROPERTY(bool tty READ tty WRITE setTty NOTIFY changed)
    Q_PROPERTY(bool stdinOnce READ stdinOnce WRITE setStdinOnce NOTIFY changed)
    Q_PROPERTY(bool startAfterCreate READ startAfterCreate WRITE setStartAfterCreate NOTIFY changed)
    /*! Whether to continue when the image is not local (the UI's "pull first"). */
    Q_PROPERTY(bool pullIfMissing READ pullIfMissing WRITE setPullIfMissing NOTIFY changed)

    /*! Ports / env vars / labels / mounts: plain data written in by the QML row editors. */
    Q_PROPERTY(QVariantList portRows READ portRows WRITE setPortRows NOTIFY changed)
    /*!
     * Status of every port row, same order as `portRows`: `{errorKey, holder, suggestion}`.
     *
     * A **property**, not `Q_INVOKABLE`: in QML a function call builds no dependency, so hints would not
     * update when the container list changes (hit repeatedly here; the port editor must immediately show
     * "held by whom / suggested port"). When free, `errorKey` is empty and the UI shows **nothing**
     * (user-requested: fewer redundant small labels).
     */
    Q_PROPERTY(QVariantList portRowStatuses READ portRowStatuses NOTIFY changed)
    Q_PROPERTY(QVariantList environmentRows READ environmentRows WRITE setEnvironmentRows NOTIFY changed)
    Q_PROPERTY(QVariantList labelRows READ labelRows WRITE setLabelRows NOTIFY changed)
    Q_PROPERTY(QVariantList mountRows READ mountRows WRITE setMountRows NOTIFY changed)

    /*! Command history (local records + existing containers' commands). */
    Q_PROPERTY(Kontainer::CommandHistoryStore *commandHistory READ commandHistory CONSTANT)
    Q_PROPERTY(QVariantList presets READ presets NOTIFY presetsChanged)
    /*!
     * Choice lists: local images and available networks.
     *
     * **Properties**, not Q_INVOKABLE: a function call builds no dependency and would also make Repeater
     * rebuild all of its items on every form change (observed to crash). They notify only when backend
     * data changes.
     */
    Q_PROPERTY(QVariantList availableImages READ availableImages NOTIFY choiceListsChanged)
    Q_PROPERTY(QVariantList availableNetworks READ availableNetworks NOTIFY choiceListsChanged)

    /*! Review summary: `[{label, value}]`, shown read-only on the last step. */
    Q_PROPERTY(QVariantList summary READ summary NOTIFY changed)

public:
    CreateContainerController(OperationController *operations,
                              MountPresetStore *presets,
                              DockerBackendInterface *backend,
                              ContainerDetailController *containerDetail = nullptr,
                              CommandHistoryStore *commandHistory = nullptr,
                              QObject *parent = nullptr);

    /*! Step key order (shared by UI and tests; do not copy it into QML). */
    static QStringList stepKeys();
    /*! As above, exposed to QML as a property (static methods are unreachable from QML). */
    Q_PROPERTY(QStringList stepKeys READ stepKeys CONSTANT)

    QString stepKey() const;
    int stepIndex() const;
    int stepCount() const;
    bool canAdvance() const;
    QString stepErrorKey() const;
    bool onSummary() const;

    QString name() const;
    QString image() const;
    QString commandText() const;
    QString entrypointText() const;
    QString workingDirectory() const;
    QString user() const;
    QString hostname() const;
    QString network() const;
    QString networkAliasesText() const;
    QString restartPolicy() const;
    int restartMaxRetries() const;
    qint64 memoryLimitBytes() const;
    double cpus() const;
    bool privileged() const;
    bool openStdin() const;
    bool tty() const;
    bool stdinOnce() const;
    bool startAfterCreate() const;
    bool pullIfMissing() const;

    QVariantList portRows() const;
    QVariantList portRowStatuses() const;
    /*! Status of one row (`portRowStatuses()` and `validatePorts()` share the same judgment). */
    QVariantMap portRowStatus(int row) const;
    QVariantList environmentRows() const;
    QVariantList labelRows() const;
    QVariantList mountRows() const;
    QVariantList presets() const;
    CommandHistoryStore *commandHistory() const
    {
        return m_commandHistory;
    }
    QVariantList summary() const;

    void setName(const QString &value);
    void setImage(const QString &value);
    void setCommandText(const QString &value);
    void setEntrypointText(const QString &value);
    void setWorkingDirectory(const QString &value);
    void setUser(const QString &value);
    void setHostname(const QString &value);
    void setNetwork(const QString &value);
    void setNetworkAliasesText(const QString &value);
    void setRestartPolicy(const QString &value);
    void setRestartMaxRetries(int value);
    void setMemoryLimitBytes(qint64 value);
    void setCpus(double value);
    void setPrivileged(bool value);
    void setOpenStdin(bool value);
    void setTty(bool value);
    void setStdinOnce(bool value);
    void setStartAfterCreate(bool value);
    void setPullIfMissing(bool value);
    void setPortRows(const QVariantList &rows);
    void setEnvironmentRows(const QVariantList &rows);
    void setLabelRows(const QVariantList &rows);
    void setMountRows(const QVariantList &rows);

    /*! Start blank (optionally pre-filled with an image, for entry from an image card/detail). */
    Q_INVOKABLE void reset(const QString &presetImage = {});
    /*!
     * Merge existing containers' commands into the history candidates (not persisted): the commands of
     * the container detail currently open.
     *
     * The container list carries no command (that needs inspect), so only the **already loaded** one is
     * taken -- inspecting every container for a single dropdown is not worth it (recorded as a deviation
     * in ARCH).
     */
    Q_INVOKABLE int mergeCommandsFromExistingContainers();
    /*! Clone: pre-fill from an existing container's **config** (no runtime state copied, §4.5). */
    Q_INVOKABLE bool prefillFromContainer(const QString &containerId);
    /*! Suggest a usable name on a conflict (`web` -> `web-copy`). */
    Q_INVOKABLE QString suggestedName() const;

    /*! Validation result of a given step (for diagnostics and tests; the current step is untouched). */
    Q_INVOKABLE QString stepErrorKeyForStep(const QString &key) const;

    /*! Previous / next step (next validates the current step first). */
    Q_INVOKABLE bool nextStep();
    Q_INVOKABLE void previousStep();
    /*! Jump to a step (for the step buttons; only up to the last validated step). */
    Q_INVOKABLE bool goToStep(const QString &key);

    QVariantList availableImages() const;
    QVariantList availableNetworks() const;

    /*!
     * Add / edit / remove port rows (phase-eight fix: row editing lives in C++).
     *
     * Why QML does not edit the list directly: with `pragma ComponentBehavior: Unbound` a `Repeater`
     * delegate **cannot see the root object's id**, so `page.xxx` inside it throws `ReferenceError`,
     * which users saw as "cannot delete a port / add a mount". With row editing in the controller a
     * delegate needs only a non-root id, and the rules are easier to test.
     */
    Q_INVOKABLE void addPortRow(int containerPort = 80, int hostPort = 0, const QString &hostIp = {}, const QString &protocol = QStringLiteral("tcp"));
    Q_INVOKABLE void setPortRow(int row, const QString &field, const QVariant &value);
    Q_INVOKABLE void removePortRow(int row);
    Q_INVOKABLE void clearPortRows();

    Q_INVOKABLE void addMountRow(const QString &type = QStringLiteral("bind"),
                                 const QString &source = {},
                                 const QString &destination = {},
                                 bool readOnly = false);
    Q_INVOKABLE void setMountRow(int row, const QString &field, const QVariant &value);
    Q_INVOKABLE void removeMountRow(int row);

    /*! Add a mount from a preset (ignored if already present). */
    Q_INVOKABLE bool addMountFromPreset(const QString &presetId);
    /*! Append an empty mount row (bind). */
    Q_INVOKABLE void addEmptyMount();
    Q_INVOKABLE void removeMountAt(int row);

    /*! Submit (last step): build the request and hand it to `OperationController::createContainer`. */
    Q_INVOKABLE bool submit();

Q_SIGNALS:
    void changed();
    void stepChanged();
    void presetsChanged();
    /*! Image or network list changed (the choice lists must be repopulated). */
    void choiceListsChanged();
    /*! Submit succeeded (the UI jumps to the new container's detail page). */
    void submitted(const QString &containerId);

private:
    /*! Validation of the current step (stable key; empty = passed). */
    QString validateCurrentStep() const;
    /*! Field-level validation of every port/mount row (the same rules `stepErrorKey` uses). */
    QString validatePorts() const;
    QString validateMounts() const;
    QVariantMap requestMap() const;
    /*! Default network: the first in the list (it arrives asynchronously, so it is computed live). */
    QString defaultNetwork() const;
    QStringList splitLines(const QString &text) const;
    void touch();

    OperationController *m_operations = nullptr;
    MountPresetStore *m_presets = nullptr;
    DockerBackendInterface *m_backend = nullptr;
    /*! Container detail controller (may be null): cloning reads command/entrypoint/env/labels from it. */
    ContainerDetailController *m_containerDetail = nullptr;
    CommandHistoryStore *m_commandHistory = nullptr;

    int m_stepIndex = 0;

    QString m_name;
    QString m_image;
    QString m_commandText;
    QString m_entrypointText;
    QString m_workingDirectory;
    QString m_user;
    QString m_hostname;
    QString m_network;
    QString m_networkAliasesText;
    QString m_restartPolicy = QStringLiteral("no");
    int m_restartMaxRetries = 0;
    qint64 m_memoryLimitBytes = 0;
    double m_cpus = 0.0;
    bool m_privileged = false;
    /* Interactive capability: on by default (see the request struct's comments) */
    bool m_openStdin = true;
    bool m_tty = true;
    bool m_stdinOnce = false;
    bool m_startAfterCreate = true;
    bool m_pullIfMissing = false;

    QVariantList m_portRows;
    QVariantList m_environmentRows;
    QVariantList m_labelRows;
    QVariantList m_mountRows;

    /*! Id returned by the engine after submit (from the `containerCreated` signal). */
    QString m_createdContainerId;
};

} // namespace Kontainer
