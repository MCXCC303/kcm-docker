/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/docker_backend_interface.h"
#include "backend/docker_capabilities.h"
#include "domain/image_pull_progress.h"
#include "model/image_build_model.h"
#include "model/image_pull_model.h"

#include <QObject>
#include <QSet>
#include <QString>

namespace Kontainer
{

class CredentialStore;

/*!
 * Orchestration and feedback for write operations (ARCH_V4 §2.2.4).
 *
 * Responsibilities:
 *  - turn UI intent (start this container / pull this image) into a backend mutation
 *  - allow one in-flight operation per target and expose the busy state to the UI
 *  - funnel success / failure / cancellation through one result channel (pages must not phrase
 *    their own messages)
 *  - maintain pull progress
 *  - enforce the write gate: no write entry point while the socket is unwritable, and a 403/EACCES
 *    at runtime downgrades this session to read-only
 *
 * Not responsible for: HTTP, JSON, path building (backend), dialogs and confirmation (QML).
 */
class OperationController : public QObject
{
    Q_OBJECT

    /* --- Operation state --- */
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY stateChanged)
    /*!
     * Revision of the busy set (incremented on every change).
     *
     * The only reason it exists: QML does not track `Q_INVOKABLE` calls, so a binding such as
     * `operations.isContainerBusy(id)` is not re-evaluated when the busy state changes. A binding
     * must also read this notifiable property to establish the dependency (see ContainerCard.qml).
     */
    Q_PROPERTY(int stateRevision READ stateRevision NOTIFY stateChanged)

    /* --- Result channel (the single source of operation results) --- */
    Q_PROPERTY(QString resultKey READ resultKey NOTIFY resultChanged)
    Q_PROPERTY(QString resultText READ resultText NOTIFY resultChanged)
    /*! Raw engine text (data, never translated); the secondary "why it failed" line. */
    Q_PROPERTY(QString resultDetailText READ resultDetailText NOTIFY resultChanged)
    /*! none / userActionable / environment / unexpected — QML picks the InlineMessage type from it. */
    Q_PROPERTY(QString resultCategoryKey READ resultCategoryKey NOTIFY resultChanged)
    /*! Optional follow-up action: empty / refresh. */
    Q_PROPERTY(QString resultActionKey READ resultActionKey NOTIFY resultChanged)

    /* --- Pull list (concurrent, keeps running in the background, ARCH_V4 §2.4) --- */
    Q_PROPERTY(Kontainer::ImagePullModel *pulls READ pulls CONSTANT)
    /*! Build list (phase 8 §5.3): like pulls (background, concurrent, cancellable, raw text kept). */
    Q_PROPERTY(Kontainer::ImageBuildModel *builds READ builds CONSTANT)
    Q_PROPERTY(bool pulling READ pulling NOTIFY pullListChanged)
    Q_PROPERTY(int activePullCount READ activePullCount NOTIFY pullListChanged)

    /* --- Write gate --- */
    Q_PROPERTY(bool writeAllowed READ writeAllowed NOTIFY writeAccessChanged)
    Q_PROPERTY(QString writeAccessKey READ writeAccessKey NOTIFY writeAccessChanged)
    Q_PROPERTY(QString writeAccessText READ writeAccessText NOTIFY writeAccessChanged)

public:
    explicit OperationController(DockerBackendInterface *backend, QObject *parent = nullptr);

    bool busy() const;
    int activeCount() const;
    int stateRevision() const;

    QString resultKey() const;
    QString resultText() const;
    QString resultDetailText() const;
    QString resultCategoryKey() const;
    QString resultActionKey() const;

    ImagePullModel *pulls() const
    {
        return m_pulls;
    }
    ImageBuildModel *builds() const
    {
        return m_builds;
    }
    /*! Whether at least one pull is running (toolbar indicator and dialog text). */
    bool pulling() const;
    int activePullCount() const;

    bool writeAllowed() const;
    QString writeAccessKey() const;
    QString writeAccessText() const;

    /*! Whether a target (`container:<id>` / `image:<ref>`) has an operation in flight. */
    Q_INVOKABLE bool isTargetBusy(const QString &targetKey) const;
    /*!
     * Whether a target has an operation in flight.
     *
     * The UI calls these helpers instead of building the `container:` / `image:` prefix itself:
     * target keys have a single definition (OperationTarget) and a typo silently breaks busy state.
     */
    Q_INVOKABLE bool isContainerBusy(const QString &id) const;
    Q_INVOKABLE bool isImageBusy(const QString &reference) const;

    Q_INVOKABLE void startContainer(const QString &id);
    Q_INVOKABLE void stopContainer(const QString &id);
    Q_INVOKABLE void restartContainer(const QString &id);
    /*! Pause a running container (user feedback ①). */
    Q_INVOKABLE void pauseContainer(const QString &id);
    /*! Resume a paused container. */
    Q_INVOKABLE void unpauseContainer(const QString &id);
    Q_INVOKABLE void removeContainer(const QString &id);
    Q_INVOKABLE void pullImage(const QString &reference);

    /*!
     * Credential source (may be null = anonymous pulls only).
     *
     * The controller only uses the read side of `CredentialStore`: when the wallet is unavailable
     * it returns empty credentials and the pull still proceeds anonymously instead of failing —
     * a private registry then answers 401 and the user sees why.
     */
    void setCredentialStore(CredentialStore *store);
    /*! Cancel one pull (the Cancel button in the list). */
    Q_INVOKABLE void cancelPull(const QString &reference);

    /*!
     * Build an image from a Dockerfile (phase 8 §5.3).
     *
     * The context directory is packed here (`packBuildContext`) and a failure yields a stable key;
     * on success the backend uploads it and the build enters the build list, which carries
     * progress, cancellation and the failure reason. A non-empty `inlineDockerfile` replaces the
     * Dockerfile in the directory (the UI can paste content directly).
     */
    Q_INVOKABLE bool buildImage(const QString &contextDirectory,
                                const QStringList &tags,
                                const QString &dockerfile = QStringLiteral("Dockerfile"),
                                const QStringList &buildArgs = {},
                                const QVariantList &labels = {},
                                const QString &target = {},
                                bool noCache = false,
                                bool pull = false,
                                const QString &inlineDockerfile = {});
    /*!
     * Prune the build cache (phase 8 §5.5): as with volume pruning, the UI shows the **reclaimable
     * space** and asks for confirmation first; this only starts it and reports the reclaimed result.
     */
    Q_INVOKABLE void pruneBuildCache();

    /*! Cancel one build (the backend deletes the temporary context when it ends). */
    Q_INVOKABLE void cancelBuild(const QString &buildId);
    /*! Drop finished build records (running ones stay). */
    Q_INVOKABLE void clearFinishedBuilds();
    /*! Cancel every in-flight pull. */
    Q_INVOKABLE void cancelAllPulls();
    /*! Remove a finished record (failed records stay until the user deals with them). */
    Q_INVOKABLE void dismissPull(const QString &reference);
    /*! Clear all finished records. */
    Q_INVOKABLE void clearFinishedPulls();
    Q_INVOKABLE void removeImage(const QString &id, bool force);

    /*!
     * Create a network (ARCH_V5_V8 §3.3).
     *
     * All validation happens in C++ (name rules, subnet/gateway format, collision with existing
     * networks) and yields a stable error key which the UI shows in the dialog. **bridge only**:
     * the UI always passes that driver.
     */
    Q_INVOKABLE bool createNetwork(const QString &name,
                                   const QString &subnet = {},
                                   const QString &gateway = {},
                                   bool internal = false,
                                   bool attachable = false,
                                   const QVariantList &labels = {});
    /*! Remove a network (the daemon rejects built-in ones, so the UI offers no entry point). */
    Q_INVOKABLE void removeNetwork(const QString &id, const QString &name = {});

    /*!
     * Connect a container to a network (ARCH_V5_V8 §3.4).
     *
     * `aliases` is a comma-separated list from a single input field: aliases let other containers
     * on the same network reach each other by name, which is stabler than IPs. Empty skips
     * `EndpointConfig`.
     */
    Q_INVOKABLE bool connectContainerToNetwork(const QString &networkId, const QString &containerId, const QString &aliases = {});
    /*! Disconnect a container (`force` stays off: never force-disconnect a network in use). */
    Q_INVOKABLE bool disconnectContainerFromNetwork(const QString &networkId, const QString &containerId);

    /*!
     * Create a container (ARCH_V5_V8 §4.6).
     *
     * The UI collects the form into a `ContainerCreateRequest`; this validates what **depends on
     * backend data** (name collision, host port conflict, image not local) and chains the two
     * steps: create, then optionally start. Each step reports separately, so a failure names
     * **which** step failed.
     *
     * `allowMissingImage` backs the "pull first" option: submit even when the image is not local.
     */
    Q_INVOKABLE bool createContainer(const QVariantMap &request, bool allowMissingImage = false);
    /*! Whether the host port is taken by an existing container (the UI can also ask before submitting). */
    Q_INVOKABLE bool hostPortInUse(const QString &hostIp, int hostPort) const;
    /*!
     * Name of the container holding that host port (empty if none).
     *
     * Only containers that **really hold** the port count: exited/created/dead ones do not, and
     * treating them as conflicts was a false report (a stopped container blocked the user).
     */
    Q_INVOKABLE QString hostPortHolder(const QString &hostIp, int hostPort) const;
    /*! Whether the name is already taken by an existing container. */
    Q_INVOKABLE bool containerNameTaken(const QString &name) const;
    /*! Whether the image is available locally (the UI then suggests pulling first). */
    Q_INVOKABLE bool imageExistsLocally(const QString &reference) const;

    /*!
     * Create a volume (ARCH_V5_V8 §3.5): name rules and duplicate checks live here; failure gives a key.
     */
    Q_INVOKABLE bool createVolume(const QString &name,
                                  const QString &driver = {},
                                  const QVariantList &labels = {});
    /*! Remove a volume (no force: when in use, let the engine refuse and explain why). */
    Q_INVOKABLE bool removeVolume(const QString &name);
    /*! Prune unused volumes (`POST /volumes/prune`); the details come back through `volumesPruned`. */
    Q_INVOKABLE bool pruneVolumes();
    /*! Volume-name validation (returns a stable key, empty = valid). */
    Q_INVOKABLE QString volumeNameError(const QString &name) const;
    /*! Whether the volume name already exists. */
    Q_INVOKABLE bool volumeNameTaken(const QString &name) const;

    /*! Dismiss the result notice (the user has read it). */
    Q_INVOKABLE void dismissResult();
    /*!
     * Clear **outdated** results on refresh/navigation: failures stay, success/cancelled/unchanged go.
     *
     * User feedback A7: the notice banner should disappear after a refresh or navigation, but
     * failure information must not clear itself.
     */
    Q_INVOKABLE void dismissResultIfObsolete();

    /*!
     * Network field validation (phase 6 §3.3): returns a stable error key, empty string = valid.
     *
     * Same pattern as image-reference validation: QML calls it for **live** validation before
     * submitting, the controller validates **again** on submit (the UI is not a security boundary).
     */
    Q_INVOKABLE QString networkNameError(const QString &name) const;
    Q_INVOKABLE QString subnetError(const QString &subnet) const;
    Q_INVOKABLE QString gatewayError(const QString &gateway, const QString &subnet) const;
    /*! Whether the name collides with an existing network (case-insensitive, matching the daemon). */
    Q_INVOKABLE bool networkNameTaken(const QString &name) const;

    /*! Validate and normalize a reference; QML calls it before submit so invalid input never leaves. */
    Q_INVOKABLE bool isValidImageReference(const QString &reference) const;
    Q_INVOKABLE QString normalizedImageReference(const QString &reference) const;
    /*! Registry address for an image reference (used to warn "not logged in to this registry"). */
    Q_INVOKABLE QString serverAddressForImage(const QString &reference) const;

    /*! Permissions may have changed (e.g. just added to the socket's group), so allow a recompute. */
    Q_INVOKABLE void refreshWriteAccess();

Q_SIGNALS:
    void stateChanged();
    void resultChanged();
    void pullListChanged();
    void writeAccessChanged();
    /*! Container removed: the detail page returns to the list. */
    void containerRemoved(const QString &id);
    /*! Container state may have changed (successful start/stop/restart): the detail page re-reads. */
    void containerStateChanged(const QString &id);
    /*! Image removed. */
    void imageRemoved(const QString &id);
    /*! Network set changed (create/remove succeeded): the network page and container detail re-read. */
    void networksChanged();
    /*! Volume set or usage changed (create / remove / prune succeeded). */
    void volumesChanged();
    /*! Container created, also for "created but start failed" from create-and-start (`started` is false). */
    void containerCreatedSignal(const QString &id, bool started);

private:
    enum class Result {
        None,
        Success,
        /*! Engine returned 304: already in the target state. */
        Unchanged,
        Error,
        Cancelled,
    };

    using Mutation = DockerBackendInterface::Mutation;
    using MutationOutcome = DockerBackendInterface::MutationOutcome;

    /*! Single admission check (permissions, busy); false means the call was rejected and reported. */
    bool admit(const QString &targetKey, const QString &what);
    /*! Currently effective write access (the degraded value wins once degraded). */
    WriteAccess effectiveWriteAccess() const;
    void beginOperation(Mutation mutation, const QString &targetKey);
    void onMutationFinished(Mutation mutation, const QString &targetKey, MutationOutcome outcome, const DockerError &error);
    void onPullProgress(const ImagePullProgress &progress);
    /*! Pull entry for a reference, nullptr if absent. */
    ImagePullEntry *findPull(const QString &reference);
    /*! Reorder and write to the model: running first, finished by end time descending (newest on top). */
    void publishPulls();
    /*! Write or update one entry in the build list (running first). */
    void publishBuild(const Kontainer::ImageBuildEntry &entry);
    void onBackendMutationFinished(Mutation mutation, const QString &targetKey, MutationOutcome outcome, const DockerError &error);

    void setResult(Result result, const QString &text, const QString &detail = QString(), const DockerError &error = DockerError());
    void setWriteAccess(WriteAccess access, bool degraded);
    /*! 403 / EACCES: downgrade this session to read-only, irreversibly. */
    void degradeToReadOnly(const DockerError &error);

    void refreshAfter(Mutation mutation, const QString &targetKey);
    /*! Result text (non-static: volume-prune text reads the last prune details). */
    QString successText(Mutation mutation, const QString &targetKey) const;
    static QString unchangedText(Mutation mutation);
    /*! Failure text: generic category text plus an operation-specific actionable hint. */
    static QString failureText(Mutation mutation, const DockerError &error);

    DockerBackendInterface *m_backend = nullptr;
    CredentialStore *m_credentialStore = nullptr;

    QSet<QString> m_busyTargets;
    int m_stateRevision = 0;
    Result m_result = Result::None;
    /*! "Create and start": whether to start after create, the new container id, step 2 in flight. */
    bool m_pendingStartAfterCreate = false;
    bool m_startAfterCreateInFlight = false;
    QString m_createdContainerId;

    /*! Bytes reclaimed by the last build-cache prune (`buildCachePruned` sets it; used in the result). */
    qint64 m_reclaimedBuildCacheBytes = -1;

    /*! Detail text of the last volume prune (`volumesPruned` records it; used in the success result). */
    QString m_pruneDetailText;
    QString m_pruneDetailList;

    QString m_resultText;
    QString m_resultDetailText;
    QString m_resultCategoryKey = QStringLiteral("none");
    QString m_resultActionKey;

    ImagePullModel *m_pulls = nullptr;
    ImageBuildModel *m_builds = nullptr;
    /*! Auto-increment counter for build ids (the UI only needs them stable and unique). */
    int m_buildCounter = 0;
    /*! UI order (running + finished); the model is rebuilt from it every time. */
    QList<ImagePullEntry> m_pullEntries;

    WriteAccess m_writeAccess = WriteAccess::Allowed;
    bool m_writeDegraded = false;
    WriteAccess m_degradedAccess = WriteAccess::SocketNotWritable;
};

} // namespace Kontainer
