/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/operation_controller.h"

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
{
    Q_ASSERT(m_backend);

    connect(m_backend, &DockerBackendInterface::mutationFinished, this, &OperationController::onBackendMutationFinished);
    connect(m_backend, &DockerBackendInterface::imagePullProgress, this, &OperationController::onPullProgress);

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
    return m_pulling;
}

QString OperationController::pullReference() const
{
    return m_pullProgress.reference;
}

QString OperationController::pullStatusText() const
{
    return m_pullProgress.statusText;
}

double OperationController::pullProgress() const
{
    return m_pulling ? m_pullProgress.fraction() : -1.0;
}

bool OperationController::pullProgressKnown() const
{
    return m_pulling && !m_pullProgress.isIndeterminate();
}

int OperationController::pullCompletedLayers() const
{
    return m_pullProgress.completedLayers;
}

int OperationController::pullTotalLayers() const
{
    return m_pullProgress.totalLayers;
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
    const QString targetKey = OperationTarget::image(normalized);
    if (!admit(targetKey, i18n("pulling the image"))) {
        return;
    }
    if (m_pulling) {
        setResult(Result::Error,
                  i18n("An image pull is already running."),
                  QString(),
                  DockerError(DockerError::Kind::PreconditionFailed));
        return;
    }

    beginOperation(Mutation::PullImage, targetKey);
    m_pulling = true;
    m_pullProgress = ImagePullProgress();
    m_pullProgress.reference = normalized;
    Q_EMIT pullChanged();
    m_backend->pullImage(normalized);
}

void OperationController::cancelPull()
{
    if (!m_pulling) {
        return;
    }
    m_backend->cancelImagePull();
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
    m_pullProgress = progress;
    m_pulling = progress.phase != ImagePullProgress::Phase::Complete && progress.phase != ImagePullProgress::Phase::Failed;
    Q_EMIT pullChanged();
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
        // 拉取结束后无论结果如何都要收起进度条（失败 / 取消的结果由 resultText 呈现）
        m_pulling = false;
        Q_EMIT pullChanged();
    }

    switch (outcome) {
    case MutationOutcome::Succeeded:
        setResult(Result::Success, successText(mutation, targetKey));
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
        setResult(Result::Error, dockerErrorText(error), error.detail(), error);
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
    }
}

QString OperationController::successText(Mutation mutation, const QString &targetKey)
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
    }
    return i18n("Done.");
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
