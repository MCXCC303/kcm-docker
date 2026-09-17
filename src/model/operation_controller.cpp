/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/operation_controller.h"

#include "model/format.h"

#include <QRegularExpression>

#include "backend/credential_store.h"
#include "backend/registry_auth.h"

#include "domain/image_reference.h"
#include "logging.h"
#include "model/docker_error_text.h"

#include <KLocalizedString>

namespace Kontainer
{

using Mutation = DockerBackendInterface::Mutation;
using MutationOutcome = DockerBackendInterface::MutationOutcome;

OperationController::OperationController(DockerBackendInterface *backend, QObject *parent)
    : QObject(parent)
    , m_backend(backend)
    , m_pulls(new ImagePullModel(this))
{
    Q_ASSERT(m_backend);

    connect(m_backend, &DockerBackendInterface::mutationFinished, this, &OperationController::onBackendMutationFinished);
    connect(m_backend, &DockerBackendInterface::imagePullProgress, this, &OperationController::onPullProgress);
    // 清理数据卷的"成功明细"（删了哪些、回收多少）：mutationFinished 只带错误，放不下这份内容
    connect(m_backend, &DockerBackendInterface::volumesPruned, this, [this](const QStringList &names, qint64 reclaimedBytes) {
        // 只**记下**明细：紧接着 mutationFinished 会走统一的结果通道，
        // 由 successText() 把这份内容当作这次清理的结果文案（直接 setResult 会被它覆盖）
        m_pruneDetailText.clear();
        m_pruneDetailList.clear();
        if (names.isEmpty()) {
            m_pruneDetailText = i18n("Nothing to clean up: no unused volume was found.");
            return;
        }
        const Format format;
        m_pruneDetailText = i18n("Cleaned up %1 volume(s) and reclaimed %2.", names.size(), format.byteSize(reclaimedBytes));
        m_pruneDetailList = names.join(QStringLiteral(", "));
    });

    refreshWriteAccess();
}

bool OperationController::busy() const
{
    return !m_busyTargets.isEmpty();
}

int OperationController::activeCount() const
{
    return int(m_busyTargets.size());
}

int OperationController::stateRevision() const
{
    return m_stateRevision;
}

QString OperationController::resultKey() const
{
    switch (m_result) {
    case Result::None:
        return QStringLiteral("none");
    case Result::Success:
        return QStringLiteral("success");
    case Result::Unchanged:
        return QStringLiteral("unchanged");
    case Result::Error:
        return QStringLiteral("error");
    case Result::Cancelled:
        return QStringLiteral("cancelled");
    }
    return QStringLiteral("none");
}

QString OperationController::resultText() const
{
    return m_resultText;
}

QString OperationController::resultDetailText() const
{
    return m_resultDetailText;
}

QString OperationController::resultCategoryKey() const
{
    return m_resultCategoryKey;
}

QString OperationController::resultActionKey() const
{
    return m_resultActionKey;
}

bool OperationController::pulling() const
{
    return m_pulls->activeCount() > 0;
}

int OperationController::activePullCount() const
{
    return m_pulls->activeCount();
}

WriteAccess OperationController::effectiveWriteAccess() const
{
    if (m_writeDegraded) {
        return m_degradedAccess;
    }
    return writeAccessFor(m_backend->endpoint());
}

bool OperationController::writeAllowed() const
{
    return writeAccessAllowed(effectiveWriteAccess());
}

QString OperationController::writeAccessKey() const
{
    // 限定命名空间：成员函数同名，不加前缀会被类作用域截住
    return Kontainer::writeAccessKey(effectiveWriteAccess());
}

QString OperationController::writeAccessText() const
{
    switch (effectiveWriteAccess()) {
    case WriteAccess::Allowed:
        // 只在降级后才有可说的话（刚才是可写的，被引擎拒绝了）
        return m_writeDegraded ? i18n("The Docker daemon refused the last write operation because of missing permissions. Kontainer switched to read-only for this session.") : QString();
    case WriteAccess::SocketNotWritable:
        return i18n("Kontainer can only read from this Docker socket: the current user is not allowed to write it. "
                    "Add the user to the group that owns the socket, or use a rootless Docker setup.");
    case WriteAccess::SocketMissing:
        return i18n("The Docker socket does not exist, so containers and images cannot be managed.");
    case WriteAccess::UnsupportedEndpoint:
        return i18n("Managing containers and images is only available for the local Docker socket.");
    }
    return {};
}

void OperationController::refreshWriteAccess()
{
    if (m_writeDegraded) {
        return;
    }
    const WriteAccess access = writeAccessFor(m_backend->endpoint());
    if (access != m_writeAccess) {
        m_writeAccess = access;
        Q_EMIT writeAccessChanged();
    }
}

void OperationController::setWriteAccess(WriteAccess access, bool degraded)
{
    const bool changed = access != m_writeAccess || degraded != m_writeDegraded;
    m_writeAccess = access;
    m_writeDegraded = degraded;
    if (changed) {
        Q_EMIT writeAccessChanged();
    }
}

void OperationController::degradeToReadOnly(const DockerError &error)
{
    if (m_writeDegraded) {
        return;
    }
    qCWarning(kontainerBackend) << "write access denied by the engine: switching this session to read-only";
    m_degradedAccess = WriteAccess::SocketNotWritable;
    setWriteAccess(m_degradedAccess, true);
    // 已经在途的操作会各自失败并给出结果，这里只需要让写入口消失
    Q_UNUSED(error)
}

bool OperationController::isTargetBusy(const QString &targetKey) const
{
    return m_busyTargets.contains(targetKey);
}

bool OperationController::isContainerBusy(const QString &id) const
{
    return isTargetBusy(OperationTarget::container(id));
}

bool OperationController::isImageBusy(const QString &reference) const
{
    return isTargetBusy(OperationTarget::image(reference));
}

bool OperationController::admit(const QString &targetKey, const QString &what)
{
    if (!writeAllowed()) {
        // 界面本应隐藏写入口；能走到这里说明有代码绕过了它，因此给出明确结果而不是静默
        setResult(Result::Error, i18n("Kontainer is in read-only mode, so %1 was not performed.", what), QString(), DockerError(DockerError::Kind::PermissionDenied));
        return false;
    }
    if (m_busyTargets.contains(targetKey)) {
        setResult(Result::Error,
                  i18n("Another operation on this object is still running."),
                  QString(),
                  DockerError(DockerError::Kind::PreconditionFailed));
        return false;
    }
    return true;
}

void OperationController::beginOperation(Mutation mutation, const QString &targetKey)
{
    m_busyTargets.insert(targetKey);
    ++m_stateRevision;
    // 新操作开始：清掉上一次的结果，避免旧提示被误读成这次的结果
    setResult(Result::None, QString());
    Q_EMIT stateChanged();
    Q_UNUSED(mutation)
}

void OperationController::setResult(Result result, const QString &text, const QString &detail, const DockerError &error)
{
    m_result = result;
    m_resultText = text;
    m_resultDetailText = detail;
    m_resultCategoryKey = error.isError() ? dockerErrorCategoryKey(error) : QStringLiteral("none");
    m_resultActionKey = error.isError() ? dockerErrorActionKey(error) : QString();
    Q_EMIT resultChanged();
}

void OperationController::dismissResult()
{
    if (m_result == Result::None) {
        return;
    }
    setResult(Result::None, QString());
}

bool OperationController::isValidImageReference(const QString &reference) const
{
    return ImageReference::isValid(reference);
}

QString OperationController::normalizedImageReference(const QString &reference) const
{
    return ImageReference::normalized(reference);
}

void OperationController::startContainer(const QString &id)
{
    const QString targetKey = OperationTarget::container(id);
    if (!admit(targetKey, i18n("starting the container"))) {
        return;
    }
    beginOperation(Mutation::StartContainer, targetKey);
    m_backend->startContainer(id);
}

void OperationController::stopContainer(const QString &id)
{
    const QString targetKey = OperationTarget::container(id);
    if (!admit(targetKey, i18n("stopping the container"))) {
        return;
    }
    beginOperation(Mutation::StopContainer, targetKey);
    m_backend->stopContainer(id);
}

void OperationController::restartContainer(const QString &id)
{
    const QString targetKey = OperationTarget::container(id);
    if (!admit(targetKey, i18n("restarting the container"))) {
        return;
    }
    beginOperation(Mutation::RestartContainer, targetKey);
    m_backend->restartContainer(id);
}

void OperationController::removeContainer(const QString &id)
{
    const QString targetKey = OperationTarget::container(id);
    if (!admit(targetKey, i18n("removing the container"))) {
        return;
    }
    beginOperation(Mutation::RemoveContainer, targetKey);
    m_backend->removeContainer(id);
}

void OperationController::pullImage(const QString &reference)
{
    if (!isValidImageReference(reference)) {
        setResult(Result::Error,
                  i18n("“%1” is not a valid image reference.", reference),
                  QString(),
                  DockerError(DockerError::Kind::PreconditionFailed));
        return;
    }
    const QString normalized = ImageReference::normalized(reference);
    if (!writeAllowed()) {
        setResult(Result::Error,
                  i18n("Kontainer is in read-only mode, so %1 was not performed.", i18n("pulling the image")),
                  QString(),
                  DockerError(DockerError::Kind::PermissionDenied));
        return;
    }
    if (m_pulls->rowForReference(normalized) >= 0) {
        setResult(Result::Error,
                  i18n("This image is already being pulled: %1", normalized),
                  QString(),
                  DockerError(DockerError::Kind::PreconditionFailed));
        return;
    }

    // 新拉取放到列表最前面，并立刻进入「进行中」状态：进度条先显示为不确定态，
    // 不阻塞界面，也不影响别的镜像拉取
    ImagePullEntry entry;
    entry.reference = normalized;
    entry.statusKey = QStringLiteral("pulling");
    entry.active = true;
    m_pullEntries.prepend(entry);
    publishPulls();

    // 拉取不再占用「操作忙碌」集合：它可能跑很久，不该让整页看起来在忙。
    // 私有仓库的凭据来自钱包（没有就是匿名拉取，引擎会回 401，用户看得见原因）
    const RegistryCredential credential = m_credentialStore ? m_credentialStore->credentialForImage(normalized) : RegistryCredential();
    m_backend->pullImage(normalized, credential);
}

QString OperationController::serverAddressForImage(const QString &reference) const
{
    return RegistryAuth::serverAddressForImage(reference);
}

void OperationController::setCredentialStore(CredentialStore *store)
{
    m_credentialStore = store;
}

QString OperationController::networkNameError(const QString &name) const
{
    return validateNetworkName(name);
}

QString OperationController::subnetError(const QString &subnet) const
{
    return validateSubnet(subnet);
}

QString OperationController::gatewayError(const QString &gateway, const QString &subnet) const
{
    return validateGateway(gateway, subnet);
}

bool OperationController::networkNameTaken(const QString &name) const
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    const QList<Network> networks = m_backend->networks();
    for (const Network &network : networks) {
        if (network.name.compare(trimmed, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

bool OperationController::createNetwork(const QString &name,
                                        const QString &subnet,
                                        const QString &gateway,
                                        bool internal,
                                        bool attachable,
                                        const QVariantList &labels)
{
    // 校验：名称规则 + 子网/网关格式 + 与现有网络重名（都在 C++ 侧，界面只显示 key）
    const QString nameError = validateNetworkName(name);
    if (!nameError.isEmpty()) {
        setResult(Result::Error, i18n("The network was not created because the name is not valid."), nameError);
        return false;
    }
    const QString subnetError = validateSubnet(subnet);
    if (!subnetError.isEmpty()) {
        setResult(Result::Error, i18n("The network was not created because the subnet is not valid."), subnetError);
        return false;
    }
    const QString gatewayError = validateGateway(gateway, subnet);
    if (!gatewayError.isEmpty()) {
        setResult(Result::Error, i18n("The network was not created because the gateway is not valid."), gatewayError);
        return false;
    }

    const QString trimmedName = name.trimmed();
    for (const Network &existing : m_backend->networks()) {
        if (existing.name.compare(trimmedName, Qt::CaseInsensitive) == 0) {
            setResult(Result::Error,
                      i18n("The network was not created because the name is already in use."),
                      QStringLiteral("nameInUse"));
            return false;
        }
    }

    if (!writeAllowed()) {
        setResult(Result::Error,
                  i18n("Kontainer is in read-only mode, so %1 was not performed.", i18n("creating the network")),
                  QString(),
                  DockerError(DockerError::Kind::PermissionDenied));
        return false;
    }

    NetworkCreateRequest request;
    request.name = trimmedName;
    request.subnet = subnet.trimmed();
    request.gateway = gateway.trimmed();
    request.internal = internal;
    request.attachable = attachable;
    for (const QVariant &entry : labels) {
        const QVariantMap map = entry.toMap();
        const QString key = map.value(QStringLiteral("key")).toString().trimmed();
        if (!key.isEmpty()) {
            request.labels.append({key, map.value(QStringLiteral("value")).toString()});
        }
    }

    // 同一个名字不允许并发提交两次
    const QString targetKey = OperationTarget::network(request.name);
    if (isTargetBusy(targetKey)) {
        setResult(Result::Error,
                  i18n("Another operation on this object is still running."),
                  QString(),
                  DockerError(DockerError::Kind::PreconditionFailed));
        return false;
    }

    beginOperation(Mutation::CreateNetwork, targetKey);
    m_backend->createNetwork(request);
    return true;
}

void OperationController::removeNetwork(const QString &id, const QString &name)
{
    if (id.isEmpty()) {
        return;
    }
    if (!writeAllowed()) {
        setResult(Result::Error,
                  i18n("Kontainer is in read-only mode, so %1 was not performed.", i18n("removing the network")),
                  QString(),
                  DockerError(DockerError::Kind::PermissionDenied));
        return;
    }
    const QString targetKey = OperationTarget::network(id);
    if (isTargetBusy(targetKey)) {
        setResult(Result::Error,
                  i18n("Another operation on this object is still running."),
                  QString(),
                  DockerError(DockerError::Kind::PreconditionFailed));
        return;
    }
    Q_UNUSED(name);
    beginOperation(Mutation::RemoveNetwork, targetKey);
    m_backend->removeNetwork(id);
}

QString OperationController::volumeNameError(const QString &name) const
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return QStringLiteral("nameRequired");
    }
    // 与 Docker 一致：字母数字开头，其余允许 . _ -
    static const QRegularExpression allowed(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.-]*$"));
    if (!allowed.match(trimmed).hasMatch()) {
        return QStringLiteral("nameInvalid");
    }
    return {};
}

bool OperationController::volumeNameTaken(const QString &name) const
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    const QList<Volume> volumes = m_backend->volumes();
    for (const Volume &volume : volumes) {
        if (volume.name == trimmed) {
            return true;
        }
    }
    return false;
}

bool OperationController::createVolume(const QString &name, const QString &driver, const QVariantList &labels)
{
    const QString nameError = volumeNameError(name);
    if (!nameError.isEmpty()) {
        setResult(Result::Error, i18n("The volume was not created because the name is not valid."), nameError);
        return false;
    }
    const QString trimmed = name.trimmed();
    if (volumeNameTaken(trimmed)) {
        setResult(Result::Error,
                  i18n("The volume was not created because the name is already in use."),
                  QStringLiteral("nameInUse"));
        return false;
    }
    if (!writeAllowed()) {
        setResult(Result::Error,
                  i18n("Kontainer is in read-only mode, so %1 was not performed.", i18n("creating the volume")),
                  QString(),
                  DockerError(DockerError::Kind::PermissionDenied));
        return false;
    }

    const QString targetKey = OperationTarget::volume(trimmed);
    if (isTargetBusy(targetKey)) {
        setResult(Result::Error,
                  i18n("Another operation on this object is still running."),
                  QString(),
                  DockerError(DockerError::Kind::PreconditionFailed));
        return false;
    }

    QList<QPair<QString, QString>> labelPairs;
    for (const QVariant &entry : labels) {
        const QVariantMap map = entry.toMap();
        const QString key = map.value(QStringLiteral("key")).toString().trimmed();
        if (!key.isEmpty()) {
            labelPairs.append({key, map.value(QStringLiteral("value")).toString()});
        }
    }

    beginOperation(Mutation::CreateVolume, targetKey);
    m_backend->createVolume(trimmed, driver, labelPairs);
    return true;
}

bool OperationController::removeVolume(const QString &name)
{
    if (name.isEmpty()) {
        return false;
    }
    if (!writeAllowed()) {
        setResult(Result::Error,
                  i18n("Kontainer is in read-only mode, so %1 was not performed.", i18n("removing the volume")),
                  QString(),
                  DockerError(DockerError::Kind::PermissionDenied));
        return false;
    }
    const QString targetKey = OperationTarget::volume(name);
    if (isTargetBusy(targetKey)) {
        setResult(Result::Error,
                  i18n("Another operation on this object is still running."),
                  QString(),
                  DockerError(DockerError::Kind::PreconditionFailed));
        return false;
    }
    beginOperation(Mutation::RemoveVolume, targetKey);
    m_backend->removeVolume(name);
    return true;
}

bool OperationController::pruneVolumes()
{
    if (!writeAllowed()) {
        setResult(Result::Error,
                  i18n("Kontainer is in read-only mode, so %1 was not performed.", i18n("cleaning up the volumes")),
                  QString(),
                  DockerError(DockerError::Kind::PermissionDenied));
        return false;
    }
    const QString targetKey = OperationTarget::volumePrune();
    if (isTargetBusy(targetKey)) {
        setResult(Result::Error,
                  i18n("Another operation on this object is still running."),
                  QString(),
                  DockerError(DockerError::Kind::PreconditionFailed));
        return false;
    }
    // 清掉上一次的明细：否则这次若没拿到明细，会显示上一次的"删了哪些"
    m_pruneDetailText.clear();
    m_pruneDetailList.clear();
    beginOperation(Mutation::PruneVolumes, targetKey);
    m_backend->pruneVolumes();
    return true;
}

bool OperationController::connectContainerToNetwork(const QString &networkId, const QString &containerId, const QString &aliases)
{
    if (networkId.isEmpty() || containerId.isEmpty()) {
        setResult(Result::Error,
                  i18n("The container was not connected because a network or container was missing."),
                  QStringLiteral("missingTarget"));
        return false;
    }
    if (!writeAllowed()) {
        setResult(Result::Error,
                  i18n("Kontainer is in read-only mode, so %1 was not performed.", i18n("connecting the container to the network")),
                  QString(),
                  DockerError(DockerError::Kind::PermissionDenied));
        return false;
    }

    QStringList aliasList;
    const QStringList parts = aliases.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty()) {
            aliasList.append(trimmed);
        }
    }

    const QString targetKey = OperationTarget::network(networkId) + QLatin1Char('/') + containerId;
    if (isTargetBusy(targetKey)) {
        setResult(Result::Error,
                  i18n("Another operation on this object is still running."),
                  QString(),
                  DockerError(DockerError::Kind::PreconditionFailed));
        return false;
    }

    beginOperation(Mutation::ConnectNetwork, targetKey);
    m_backend->connectNetwork(networkId, containerId, aliasList);
    return true;
}

bool OperationController::disconnectContainerFromNetwork(const QString &networkId, const QString &containerId)
{
    if (networkId.isEmpty() || containerId.isEmpty()) {
        setResult(Result::Error,
                  i18n("The container was not disconnected because a network or container was missing."),
                  QStringLiteral("missingTarget"));
        return false;
    }
    if (!writeAllowed()) {
        setResult(Result::Error,
                  i18n("Kontainer is in read-only mode, so %1 was not performed.", i18n("disconnecting the container from the network")),
                  QString(),
                  DockerError(DockerError::Kind::PermissionDenied));
        return false;
    }

    const QString targetKey = OperationTarget::network(networkId) + QLatin1Char('/') + containerId;
    if (isTargetBusy(targetKey)) {
        setResult(Result::Error,
                  i18n("Another operation on this object is still running."),
                  QString(),
                  DockerError(DockerError::Kind::PreconditionFailed));
        return false;
    }

    beginOperation(Mutation::DisconnectNetwork, targetKey);
    m_backend->disconnectNetwork(networkId, containerId);
    return true;
}

void OperationController::cancelPull(const QString &reference)
{
    const QString normalized = ImageReference::normalized(reference);
    if (m_pulls->rowForReference(normalized) < 0) {
        return;
    }
    m_backend->cancelImagePull(normalized);
}

void OperationController::cancelAllPulls()
{
    m_backend->cancelAllImagePulls();
}

void OperationController::dismissPull(const QString &reference)
{
    const QString normalized = ImageReference::normalized(reference);
    for (int i = 0; i < m_pullEntries.size(); ++i) {
        const ImagePullEntry &entry = m_pullEntries.at(i);
        if (entry.reference == normalized && entry.isFinished()) {
            m_pullEntries.removeAt(i);
            publishPulls();
            return;
        }
    }
}

void OperationController::clearFinishedPulls()
{
    QList<ImagePullEntry> kept;
    for (const ImagePullEntry &entry : std::as_const(m_pullEntries)) {
        if (entry.active) {
            kept.append(entry);
        }
    }
    if (kept.size() == m_pullEntries.size()) {
        return;
    }
    m_pullEntries = kept;
    publishPulls();
}

ImagePullEntry *OperationController::findPull(const QString &reference)
{
    const QString normalized = ImageReference::normalized(reference);
    for (ImagePullEntry &entry : m_pullEntries) {
        if (entry.reference == normalized) {
            return &entry;
        }
    }
    return nullptr;
}

void OperationController::publishPulls()
{
    // 进行中的在前，已结束的保持「最近结束的在前」
    QList<ImagePullEntry> ordered;
    ordered.reserve(m_pullEntries.size());
    for (const ImagePullEntry &entry : std::as_const(m_pullEntries)) {
        if (entry.active) {
            ordered.append(entry);
        }
    }
    for (const ImagePullEntry &entry : std::as_const(m_pullEntries)) {
        if (!entry.active) {
            ordered.append(entry);
        }
    }
    m_pulls->setEntries(ordered);
    Q_EMIT pullListChanged();
}

void OperationController::removeImage(const QString &id, bool force)
{
    const QString targetKey = OperationTarget::image(id);
    if (!admit(targetKey, i18n("removing the image"))) {
        return;
    }
    beginOperation(Mutation::RemoveImage, targetKey);
    m_backend->removeImage(id, force);
}

void OperationController::onPullProgress(const ImagePullProgress &progress)
{
    if (progress.reference.isEmpty()) {
        return;
    }
    ImagePullEntry *entry = findPull(progress.reference);
    if (!entry) {
        return;
    }
    entry->statusText = progress.statusText;
    entry->progressKnown = !progress.isIndeterminate();
    entry->progress = entry->progressKnown ? progress.fraction() : -1.0;
    entry->completedLayers = progress.completedLayers;
    entry->totalLayers = progress.totalLayers;
    publishPulls();
}

void OperationController::onBackendMutationFinished(Mutation mutation,
                                                   const QString &targetKey,
                                                   MutationOutcome outcome,
                                                   const DockerError &error)
{
    onMutationFinished(mutation, targetKey, outcome, error);
}

void OperationController::onMutationFinished(Mutation mutation,
                                             const QString &targetKey,
                                             MutationOutcome outcome,
                                             const DockerError &error)
{
    const bool wasBusy = m_busyTargets.remove(targetKey);
    if (wasBusy) {
        ++m_stateRevision;
        Q_EMIT stateChanged();
    }

    if (mutation == Mutation::PullImage) {
        // 拉取结束：更新列表里的那一条（成功 / 失败 / 取消都保留在列表里，
        // 失败原因因此不会被静默丢掉，用户处理完再手动移除）
        const QString reference = targetKey.section(QLatin1Char(':'), 1);
        ImagePullEntry *entry = findPull(reference);
        if (entry) {
            entry->active = false;
            switch (outcome) {
            case MutationOutcome::Succeeded:
            case MutationOutcome::Unchanged:
                entry->statusKey = QStringLiteral("succeeded");
                entry->progressKnown = true;
                entry->progress = 1.0;
                break;
            case MutationOutcome::Cancelled:
                entry->statusKey = QStringLiteral("cancelled");
                break;
            case MutationOutcome::Failed:
                entry->statusKey = QStringLiteral("failed");
                entry->detailText = error.detail();
                entry->errorKindKey = DockerError::kindKey(error.kind());
                break;
            }
        }
        publishPulls();
    }

    switch (outcome) {
    case MutationOutcome::Succeeded:
        if (mutation == Mutation::PruneVolumes) {
            // 清理的明细（卷名列表）放在"技术细节"行里，用户可以核对删了什么
            setResult(Result::Success, successText(mutation, targetKey), m_pruneDetailList);
        } else {
            setResult(Result::Success, successText(mutation, targetKey));
        }
        refreshAfter(mutation, targetKey);
        break;
    case MutationOutcome::Unchanged:
        setResult(Result::Unchanged, unchangedText(mutation));
        refreshAfter(mutation, targetKey);
        break;
    case MutationOutcome::Cancelled:
        setResult(Result::Cancelled,
                  mutation == Mutation::PullImage ? i18n("Image pull cancelled.") : i18n("Operation cancelled."));
        break;
    case MutationOutcome::Failed: {
        // 权限被拒 → 本次会话降级为只读（不可逆）
        if (error.kind() == DockerError::Kind::PermissionDenied) {
            degradeToReadOnly(error);
        }
        setResult(Result::Error, failureText(mutation, error), error.detail(), error);
        break;
    }
    }
}

void OperationController::refreshAfter(Mutation mutation, const QString &targetKey)
{
    switch (mutation) {
    case Mutation::StartContainer:
    case Mutation::StopContainer:
    case Mutation::RestartContainer: {
        const QString id = targetKey.section(QLatin1Char(':'), 1);
        m_backend->refreshContainers();
        m_backend->refreshStorageUsage();
        Q_EMIT containerStateChanged(id);
        break;
    }
    case Mutation::RemoveContainer: {
        const QString id = targetKey.section(QLatin1Char(':'), 1);
        m_backend->refreshContainers();
        m_backend->refreshStorageUsage();
        Q_EMIT containerRemoved(id);
        break;
    }
    case Mutation::PullImage:
        // 拉取成功后镜像列表与存储占用都会变
        m_backend->refreshImages();
        m_backend->refreshStorageUsage();
        break;
    case Mutation::RemoveImage: {
        const QString id = targetKey.section(QLatin1Char(':'), 1);
        m_backend->refreshImages();
        m_backend->refreshStorageUsage();
        Q_EMIT imageRemoved(id);
        break;
    }
    case Mutation::CreateNetwork:
        m_backend->refreshNetworks();
        Q_EMIT networksChanged();
        break;
    case Mutation::RemoveNetwork:
        m_backend->refreshNetworks();
        // 网络被删掉后容器的网络信息也变了（详情页要重读）
        m_backend->refreshContainers();
        Q_EMIT networksChanged();
        break;
    case Mutation::CreateVolume:
    case Mutation::RemoveVolume:
    case Mutation::PruneVolumes:
        m_backend->refreshVolumes(false); // 只要列表：占用由 storage 那次刷新负责
        m_backend->refreshStorageUsage();
        Q_EMIT volumesChanged();
        break;
    case Mutation::ConnectNetwork:
    case Mutation::DisconnectNetwork: {
        // 网络成员列表与容器详情的网络分区都会变：两边都重读
        const QString containerId = targetKey.section(QLatin1Char('/'), 1);
        m_backend->refreshNetworks();
        m_backend->refreshContainers();
        Q_EMIT networksChanged();
        Q_EMIT containerStateChanged(containerId);
        break;
    }
    }
}

QString OperationController::successText(Mutation mutation, const QString &targetKey) const
{
    switch (mutation) {
    case Mutation::StartContainer:
        return i18n("Container started.");
    case Mutation::StopContainer:
        return i18n("Container stopped.");
    case Mutation::RestartContainer:
        return i18n("Container restarted.");
    case Mutation::RemoveContainer:
        return i18n("Container removed.");
    case Mutation::PullImage: {
        const QString reference = targetKey.section(QLatin1Char(':'), 1);
        return i18n("Image pulled: %1", ImageReference::shortForm(reference));
    }
    case Mutation::RemoveImage:
        return i18n("Image removed.");
    case Mutation::CreateNetwork: {
        const QString name = targetKey.section(QLatin1Char(':'), 1);
        return i18n("Network created: %1", name);
    }
    case Mutation::RemoveNetwork:
        return i18n("Network removed.");
    case Mutation::ConnectNetwork:
        return i18n("Container connected to the network.");
    case Mutation::DisconnectNetwork:
        return i18n("Container disconnected from the network.");
    case Mutation::CreateVolume: {
        const QString name = targetKey.section(QLatin1Char(':'), 1);
        return i18n("Volume created: %1", name);
    }
    case Mutation::RemoveVolume:
        return i18n("Volume removed.");
    case Mutation::PruneVolumes:
        // 明细（删了哪些、回收多少）由 volumesPruned 先记下来，这里用它当结果文案
        return m_pruneDetailText.isEmpty() ? i18n("Unused volumes cleaned up.") : m_pruneDetailText;
    }
    return i18n("Done.");
}

QString OperationController::failureText(Mutation mutation, const DockerError &error)
{
    const QString base = dockerErrorText(error);
    // 拉取卡住（引擎联系不上镜像仓库）时，光说「超时」用户不知道能做什么
    if (mutation == Mutation::PullImage && error.kind() == DockerError::Kind::Timeout) {
        return i18n("%1 The registry may be unreachable from the Docker daemon (network, proxy, or IPv6 routing).",
                    base);
    }
    return base;
}

QString OperationController::unchangedText(Mutation mutation)
{
    switch (mutation) {
    case Mutation::StartContainer:
        return i18n("The container is already running.");
    case Mutation::StopContainer:
        return i18n("The container is already stopped.");
    default:
        return i18n("Nothing to do: the object is already in the requested state.");
    }
}

} // namespace Kontainer
