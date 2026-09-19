/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/operation_controller.h"

#include <cstring>

#include "model/format.h"

#include <QRegularExpression>

#include "backend/build_context.h"
#include "backend/credential_store.h"
#include "backend/registry_auth.h"

#include "domain/image_reference.h"
#include "logging.h"
#include "model/docker_error_text.h"

#include <KFormat>
#include <KLocalizedString>

namespace Kontainer
{

namespace
{
/*! QVariant（QStringList 或 QVariantList）→ QStringList：界面可能用任一种表达。 */
QStringList stringListFromVariant(const QVariant &value)
{
    QStringList result;
    const QVariantList list = value.toList();
    for (const QVariant &item : list) {
        const QString text = item.toString();
        if (!text.isEmpty()) {
            result.append(text);
        }
    }
    return result;
}
} // namespace

using Mutation = DockerBackendInterface::Mutation;
using MutationOutcome = DockerBackendInterface::MutationOutcome;

OperationController::OperationController(DockerBackendInterface *backend, QObject *parent)
    : QObject(parent)
    , m_backend(backend)
    , m_pulls(new ImagePullModel(this))
    , m_builds(new ImageBuildModel(this))
{
    Q_ASSERT(m_backend);

    connect(m_backend, &DockerBackendInterface::mutationFinished, this, &OperationController::onBackendMutationFinished);
    connect(m_backend, &DockerBackendInterface::imagePullProgress, this, &OperationController::onPullProgress);
    // 清理数据卷的"成功明细"（删了哪些、回收多少）：mutationFinished 只带错误，放不下这份内容
    // 创建成功才能拿到 id；"创建并启动"在这里串行发起第二步（两步结果分别呈现）
    // 构建进度：逐行更新列表里的那一条（失败原因由后端拼好失败步骤）
    connect(m_backend, &DockerBackendInterface::imageBuildProgress, this, [this](const QString &buildId, const ImageBuildUpdate &update) {
        const int row = m_builds->rowForBuildId(buildId);
        if (row < 0) {
            return;
        }
        ImageBuildEntry entry = m_builds->entries().at(row);
        if (!update.statusText.isEmpty()) {
            entry.statusText = update.statusText;
        }
        entry.stepIndex = update.stepIndex;
        entry.totalSteps = update.totalSteps;
        entry.stepCommand = update.stepCommand;
        if (update.progressKnown) {
            entry.progress = update.progress;
            entry.progressKnown = true;
        }
        if (!update.errorText.isEmpty()) {
            entry.detailText = update.errorText;
        }
        publishBuild(entry);
    });

    connect(m_backend,
            &DockerBackendInterface::imageBuildFinished,
            this,
            [this](const QString &buildId, MutationOutcome outcome, const DockerError &error, const QString &imageId) {
                const int row = m_builds->rowForBuildId(buildId);
                if (row < 0) {
                    return;
                }
                ImageBuildEntry entry = m_builds->entries().at(row);
                entry.active = false;
                switch (outcome) {
                case MutationOutcome::Succeeded:
                case MutationOutcome::Unchanged:
                    entry.statusKey = QStringLiteral("succeeded");
                    entry.progress = 1.0;
                    entry.progressKnown = true;
                    entry.imageId = imageId;
                    setResult(Result::Success,
                              entry.tags.isEmpty() ? i18n("Image built.") : i18n("Image built: %1", entry.tags.first()));
                    break;
                case MutationOutcome::Cancelled:
                    entry.statusKey = QStringLiteral("cancelled");
                    break;
                case MutationOutcome::Failed:
                    entry.statusKey = QStringLiteral("failed");
                    // 失败原因里已经带上了失败的步骤（后端拼的），没拼上时退回引擎原文
                    entry.detailText = entry.detailText.isEmpty() ? error.detail() : entry.detailText;
                    entry.errorKindKey = DockerError::kindKey(error.kind());
                    setResult(Result::Error, failureText(Mutation::BuildImage, error), entry.detailText, error);
                    break;
                }
                publishBuild(entry);
            });

    connect(m_backend, &DockerBackendInterface::containerCreated, this, [this](const QString &id, const QString &warning) {
        m_createdContainerId = id;
        const bool startNow = m_pendingStartAfterCreate;
        Q_EMIT containerCreatedSignal(id, false); // 先报"已创建"；启动成功后再报一次 started=true
        if (startNow) {
            // 第二步：启动。完成时（成功或失败）文案都要说明"这是创建之后的启动"
            m_startAfterCreateInFlight = true;
            m_backend->startContainer(id);
        }
        if (!warning.isEmpty()) {
            qCWarning(kontainerModel) << "container create warning:" << warning;
        }
    });

    connect(m_backend, &DockerBackendInterface::buildCachePruned, this, [this](qint64 reclaimedBytes) {
        m_reclaimedBuildCacheBytes = reclaimedBytes;
    });

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

void OperationController::dismissResultIfObsolete()
{
    switch (m_result) {
    case Result::None:
        return;
    case Result::Success:
    case Result::Unchanged:
    case Result::Cancelled:
        setResult(Result::None, QString());
        return;
    case Result::Error:
        return; // 失败留着：用户还要看原因
    }
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

void OperationController::pauseContainer(const QString &id)
{
    if (!writeAllowed()) {
        setResult(Result::Error,
                  i18n("Kontainer is in read-only mode, so %1 was not performed.", i18n("pausing the container")),
                  QString(),
                  DockerError(DockerError::Kind::PermissionDenied));
        return;
    }
    beginOperation(Mutation::PauseContainer, OperationTarget::container(id));
    m_backend->pauseContainer(id);
}

void OperationController::unpauseContainer(const QString &id)
{
    if (!writeAllowed()) {
        setResult(Result::Error,
                  i18n("Kontainer is in read-only mode, so %1 was not performed.", i18n("resuming the container")),
                  QString(),
                  DockerError(DockerError::Kind::PermissionDenied));
        return;
    }
    beginOperation(Mutation::UnpauseContainer, OperationTarget::container(id));
    m_backend->unpauseContainer(id);
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

bool OperationController::imageExistsLocally(const QString &reference) const
{
    if (reference.isEmpty()) {
        return false;
    }
    // 按引用与 id 都能匹配：界面可能选的是列表里的镜像（带 tag），也可能直接填了 id
    const QList<Image> images = m_backend->images();
    for (const Image &image : images) {
        if (image.id == reference) {
            return true;
        }
        for (const QString &tag : image.repoTags) {
            if (tag == reference) {
                return true;
            }
        }
    }
    return false;
}

bool OperationController::containerNameTaken(const QString &name) const
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    // 引擎允许更长的名字，界面按完整名字比对；容器名前缀 `/` 是 docker CLI 的写法，这里不涉及
    const QList<Container> containers = m_backend->containers();
    for (const Container &container : containers) {
        if (container.name.compare(trimmed, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

namespace
{
/*!
 * 该状态的容器是否**真的占着**宿主端口。
 *
 * 只有跑着的容器才持有端口：没启动（Created）、已退出（Exited）、已死（Dead）的都不占，
 * 状态读不出来（`Unknown`）时也按**不占**算——用户明确要求这样：
 * 没运行自然不会占用，按"占用"拦下来反而会挡住其它应用使用它真正需要的端口。
 * 代价是极端情况下会漏报，那种情况由启动时的错误文案兜底（见 `failureText()`）。
 */
bool holdsHostPorts(ContainerState state)
{
    switch (state) {
    case ContainerState::Running:
    case ContainerState::Paused:
    case ContainerState::Restarting:
        return true;
    default:
        return false;
    }
}

/*! 两个绑定地址是否有交集（0.0.0.0 与任何地址都冲突；IPv6 通配同理）。 */
bool hostBindingsOverlap(const QString &lhs, const QString &rhs)
{
    const QString left = lhs.isEmpty() ? QStringLiteral("0.0.0.0") : lhs;
    const QString right = rhs.isEmpty() ? QStringLiteral("0.0.0.0") : rhs;
    const auto isWildcard = [](const QString &value) {
        return value == QLatin1String("0.0.0.0") || value == QLatin1String("::") || value == QLatin1String("[::]");
    };
    if (isWildcard(left) || isWildcard(right)) {
        /*
         * 通配之间也要按协议族看：`0.0.0.0:8100` 与 `[::]:8100` 在 Linux 上默认
         * 是**互相冲突**的（除非 net.ipv6.bindv6only=1），因此一律算冲突——
         * 宁可提示得保守一点，也好过让用户在运行时报"port is already allocated"。
         */
        return true;
    }
    return left == right;
}
} // namespace

QString OperationController::hostPortHolder(const QString &hostIp, int hostPort) const
{
    if (hostPort <= 0) {
        return {}; // 0 = 随机分配，不冲突
    }
    for (const Container &container : m_backend->containers()) {
        if (!holdsHostPorts(container.state)) {
            continue;
        }
        for (const Port &port : container.ports) {
            if (port.isPublished() && port.publicPort == hostPort && hostBindingsOverlap(port.ip, hostIp)) {
                return container.name.isEmpty() ? container.shortId() : container.name;
            }
        }
    }
    return {};
}

bool OperationController::hostPortInUse(const QString &hostIp, int hostPort) const
{
    return !hostPortHolder(hostIp, hostPort).isEmpty();
}

bool OperationController::createContainer(const QVariantMap &request, bool allowMissingImage)
{
    const QString name = request.value(QStringLiteral("name")).toString().trimmed();
    const QString image = request.value(QStringLiteral("image")).toString().trimmed();

    // 1) 名称与镜像：名称规则 / 重名 / 镜像必须存在
    const QString nameError = validateContainerName(name);
    if (!nameError.isEmpty()) {
        setResult(Result::Error, i18n("The container was not created because the name is not valid."), nameError);
        return false;
    }
    if (containerNameTaken(name)) {
        setResult(Result::Error,
                  i18n("The container was not created because the name is already in use."),
                  QStringLiteral("nameInUse"));
        return false;
    }
    if (image.isEmpty()) {
        setResult(Result::Error,
                  i18n("The container was not created because no image was chosen."),
                  QStringLiteral("imageRequired"));
        return false;
    }
    if (!allowMissingImage && !imageExistsLocally(image)) {
        setResult(Result::Error,
                  i18n("The container was not created because the image is not available locally."),
                  QStringLiteral("imageNotLocal"));
        return false;
    }

    // 2) 端口冲突：对照现有容器（引擎也会拒绝，但在这里挡住能给出更清楚的提示）
    const QVariantList ports = request.value(QStringLiteral("ports")).toList();
    for (const QVariant &entry : ports) {
        const QVariantMap port = entry.toMap();
        const int hostPort = port.value(QStringLiteral("hostPort")).toInt();
        const QString hostIp = port.value(QStringLiteral("hostIp")).toString();
        const QString holder = hostPortHolder(hostIp, hostPort);
        if (!holder.isEmpty()) {
            setResult(Result::Error,
                      i18n("The container was not created because host port %1 is already used by “%2”. Stop that container first.", hostPort, holder),
                      QStringLiteral("portInUse"));
            return false;
        }
    }

    // 3) 挂载与环境变量的字段级校验
    const QVariantList mounts = request.value(QStringLiteral("mounts")).toList();
    for (const QVariant &entry : mounts) {
        const QVariantMap mount = entry.toMap();
        const QString destination = mount.value(QStringLiteral("destination")).toString();
        const QString pathError = validateContainerPath(destination);
        if (!pathError.isEmpty()) {
            setResult(Result::Error,
                      i18n("The container was not created because a mount destination is not valid."),
                      pathError);
            return false;
        }
    }

    if (!writeAllowed()) {
        setResult(Result::Error,
                  i18n("Kontainer is in read-only mode, so %1 was not performed.", i18n("creating the container")),
                  QString(),
                  DockerError(DockerError::Kind::PermissionDenied));
        return false;
    }

    const QString targetKey = OperationTarget::container(name);
    if (isTargetBusy(targetKey)) {
        setResult(Result::Error,
                  i18n("Another operation on this object is still running."),
                  QString(),
                  DockerError(DockerError::Kind::PreconditionFailed));
        return false;
    }

    // 4) QVariantMap → 请求结构（界面只传表单字段，映射细节在这里收口）
    ContainerCreateRequest create;
    create.name = name;
    create.image = image;
    create.command = stringListFromVariant(request.value(QStringLiteral("command")));
    create.entrypoint = stringListFromVariant(request.value(QStringLiteral("entrypoint")));
    create.environment = stringListFromVariant(request.value(QStringLiteral("environment")));
    create.workingDirectory = request.value(QStringLiteral("workingDirectory")).toString();
    create.user = request.value(QStringLiteral("user")).toString();
    create.hostname = request.value(QStringLiteral("hostname")).toString();
    create.network = request.value(QStringLiteral("network")).toString();
    create.networkAliases = stringListFromVariant(request.value(QStringLiteral("networkAliases")));
    create.restartPolicy = request.value(QStringLiteral("restartPolicy"), QStringLiteral("no")).toString();
    create.restartMaxRetries = request.value(QStringLiteral("restartMaxRetries")).toInt();
    create.memoryLimitBytes = request.value(QStringLiteral("memoryLimitBytes")).toLongLong();
    create.cpus = request.value(QStringLiteral("cpus")).toDouble();
    create.privileged = request.value(QStringLiteral("privileged")).toBool();
    create.openStdin = request.value(QStringLiteral("openStdin")).toBool();
    create.tty = request.value(QStringLiteral("tty")).toBool();
    create.stdinOnce = request.value(QStringLiteral("stdinOnce")).toBool();
    create.startAfterCreate = request.value(QStringLiteral("startAfterCreate")).toBool();
    for (const QVariant &entry : request.value(QStringLiteral("labels")).toList()) {
        const QVariantMap label = entry.toMap();
        const QString key = label.value(QStringLiteral("key")).toString().trimmed();
        if (!key.isEmpty()) {
            create.labels.append({key, label.value(QStringLiteral("value")).toString()});
        }
    }
    for (const QVariant &entry : ports) {
        const QVariantMap port = entry.toMap();
        ContainerPortRequest portRequest;
        portRequest.hostIp = port.value(QStringLiteral("hostIp")).toString();
        portRequest.hostPort = quint16(port.value(QStringLiteral("hostPort")).toUInt());
        portRequest.containerPort = quint16(port.value(QStringLiteral("containerPort")).toUInt());
        portRequest.protocol = port.value(QStringLiteral("protocol"), QStringLiteral("tcp")).toString();
        create.ports.append(portRequest);
    }
    for (const QVariant &entry : mounts) {
        const QVariantMap mount = entry.toMap();
        ContainerMountRequest mountRequest;
        mountRequest.type = mount.value(QStringLiteral("type"), QStringLiteral("bind")).toString();
        mountRequest.source = mount.value(QStringLiteral("source")).toString();
        mountRequest.destination = mount.value(QStringLiteral("destination")).toString();
        mountRequest.readOnly = mount.value(QStringLiteral("readOnly")).toBool();
        create.mounts.append(mountRequest);
    }

    m_pendingStartAfterCreate = create.startAfterCreate;
    m_createdContainerId.clear();
    beginOperation(Mutation::CreateContainer, targetKey);
    m_backend->createContainer(create);
    return true;
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

bool OperationController::buildImage(const QString &contextDirectory,
                                     const QStringList &tags,
                                     const QString &dockerfile,
                                     const QStringList &buildArgs,
                                     const QVariantList &labels,
                                     const QString &target,
                                     bool noCache,
                                     bool pull,
                                     const QString &inlineDockerfile)
{
    if (!writeAllowed()) {
        setResult(Result::Error,
                  i18n("Kontainer is in read-only mode, so %1 was not performed.", i18n("building the image")),
                  QString(),
                  DockerError(DockerError::Kind::PermissionDenied));
        return false;
    }
    if (tags.isEmpty() || tags.first().trimmed().isEmpty()) {
        setResult(Result::Error, i18n("The image was not built because no tag was given."), QStringLiteral("tagRequired"));
        return false;
    }

    // 上下文先打包：本地能发现的错误（目录不存在、没有 Dockerfile、太大）不必等引擎
    BuildContextOptions options;
    options.directory = contextDirectory;
    options.dockerfile = dockerfile.isEmpty() ? QStringLiteral("Dockerfile") : dockerfile;
    options.inlineDockerfile = inlineDockerfile;
    const BuildContextResult context = packBuildContext(options);
    if (!context.ok) {
        setResult(Result::Error, i18n("The build context could not be packaged."), context.errorKey);
        return false;
    }

    ImageBuildRequest request;
    request.id = QStringLiteral("build-%1").arg(++m_buildCounter);
    request.contextArchive = context.archivePath;
    request.contextDirectory = contextDirectory;
    request.dockerfile = options.dockerfile;
    request.tags = tags;
    request.buildArgs = buildArgs;
    request.target = target;
    request.noCache = noCache;
    request.pull = pull;
    for (const QVariant &entry : labels) {
        const QVariantMap label = entry.toMap();
        const QString key = label.value(QStringLiteral("key")).toString().trimmed();
        if (!key.isEmpty()) {
            request.labels.append({key, label.value(QStringLiteral("value")).toString()});
        }
    }
    // 私有基础镜像：凭据查询与拉取走同一条路径（八期 §5.3）
    const RegistryCredential credential = m_credentialStore
        ? m_credentialStore->credentialForImage(tags.first().trimmed())
        : RegistryCredential();
    if (!credential.isEmpty() && !credential.username.isEmpty()) {
        RegistryCredential outgoing = credential;
        const QString registry = RegistryAuth::serverAddressForImage(tags.first().trimmed());
        if (!registry.isEmpty()) {
            outgoing.serverAddress = registry;
        }
        request.registryAuthHeader = RegistryAuth::encode(outgoing);
    }

    ImageBuildEntry entry;
    entry.id = request.id;
    entry.contextDirectory = contextDirectory;
    entry.tags = tags;
    entry.statusKey = QStringLiteral("building");
    entry.statusText = i18n("Uploading the build context…");
    entry.active = true;
    publishBuild(entry);

    m_backend->buildImage(request);
    return true;
}

void OperationController::pruneBuildCache()
{
    if (!writeAllowed()) {
        setResult(Result::Error,
                  i18n("Kontainer is in read-only mode, so %1 was not performed.", i18n("cleaning the build cache")),
                  QString(),
                  DockerError(DockerError::Kind::PermissionDenied));
        return;
    }
    m_reclaimedBuildCacheBytes = -1;
    beginOperation(Mutation::PruneBuildCache, QStringLiteral("buildCache:"));
    m_backend->pruneBuildCache();
}

void OperationController::cancelBuild(const QString &buildId)
{
    m_backend->cancelImageBuild(buildId);
}

void OperationController::clearFinishedBuilds()
{
    QList<ImageBuildEntry> kept;
    for (const ImageBuildEntry &entry : m_builds->entries()) {
        if (entry.active) {
            kept.append(entry);
        }
    }
    m_builds->setEntries(kept);
}

void OperationController::publishBuild(const ImageBuildEntry &entry)
{
    QList<ImageBuildEntry> entries = m_builds->entries();
    const int row = m_builds->rowForBuildId(entry.id);
    if (row >= 0) {
        entries[row] = entry;
    } else {
        entries.append(entry);
    }
    // 进行中的在前，已结束的排在后面（与拉取列表一致）
    std::stable_sort(entries.begin(), entries.end(), [](const ImageBuildEntry &lhs, const ImageBuildEntry &rhs) {
        return lhs.active && !rhs.active;
    });
    m_builds->setEntries(entries);
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
        if (m_startAfterCreateInFlight && mutation == Mutation::StartContainer) {
            m_startAfterCreateInFlight = false;
            Q_EMIT containerCreatedSignal(m_createdContainerId, true);
            setResult(Result::Success, i18n("Container created and started: %1", targetKey.section(QLatin1Char(':'), 1)));
            refreshAfter(mutation, targetKey);
            break;
        }
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
        if (m_startAfterCreateInFlight && mutation == Mutation::StartContainer) {
            // 创建成功、启动失败：必须说清是哪一步失败（§4.6）
            m_startAfterCreateInFlight = false;
            setResult(Result::Error,
                      i18n("The container was created but could not be started: %1", dockerErrorText(error)),
                      error.detail(),
                      error);
            refreshAfter(Mutation::CreateContainer, targetKey);
            break;
        }
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
    case Mutation::PauseContainer:
    case Mutation::UnpauseContainer:
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
    case Mutation::CreateContainer:
        // 创建成功后容器列表与存储占用都会变；"创建并启动"由 containerCreated 的
        // 回调串行发起启动（见构造函数里的连接）
        m_backend->refreshContainers();
        m_backend->refreshStorageUsage();
        break;
    case Mutation::BuildImage:
        // 构建成功会多出镜像与构建缓存；失败也无所谓，刷新一次不贵
        m_backend->refreshImages();
        m_backend->refreshStorageUsage();
        break;
    case Mutation::PruneBuildCache:
        // 清理后存储占用变了（构建缓存那一段）
        m_backend->refreshStorageUsage();
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
    case Mutation::PauseContainer:
        return i18n("Container paused.");
    case Mutation::UnpauseContainer:
        return i18n("Container resumed.");
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
    case Mutation::CreateContainer: {
        // 只声明"已创建"：启动是第二步，成功或失败都会另给一条结果（§4.6）
        const QString name = targetKey.section(QLatin1Char(':'), 1);
        return i18n("Container created: %1", name);
    }
    case Mutation::BuildImage: {
        // 构建的进度与结果主要在构建列表里；这里只给一句总的结果
        return i18n("Image built.");
    }
    case Mutation::PruneBuildCache:
        if (m_reclaimedBuildCacheBytes == 0) {
            return i18n("No build cache to clean up.");
        }
        return i18n("Build cache cleaned up, %1 reclaimed.", KFormat().formatByteSize(double(m_reclaimedBuildCacheBytes)));
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

    /*
     * 宿主端口已被占用（启动时才暴露，创建请求本身是合法的）：
     * 引擎原文是 `driver failed programming external connectivity … Bind for 0.0.0.0:8100 failed:
     * port is already allocated`，普通用户读不出"我该做什么"。这里翻成一句可行动的说明，
     * 并把端口号提出来（能提出来才翻，提不出来就保留原文，不猜）。
     */
    const QString detail = error.detail();
    if (detail.contains(QLatin1String("port is already allocated"), Qt::CaseInsensitive)) {
        const int bindAt = detail.indexOf(QLatin1String("Bind for "), 0, Qt::CaseInsensitive);
        if (bindAt >= 0) {
            const QString afterBind = detail.mid(bindAt + int(strlen("Bind for ")));
            const QString binding = afterBind.left(afterBind.indexOf(QLatin1String(" failed")));
            if (!binding.isEmpty()) {
                return i18n("The container could not be started because host port %1 is already used by another container. Choose a different host port or stop that container.", binding);
            }
        }
        return i18n("The container could not be started because a host port is already used by another container. Choose a different host port or stop that container.");
    }
    // 拉取卡住（引擎联系不上镜像仓库）时，光说「超时」用户不知道能做什么
    if (mutation == Mutation::PullImage && error.kind() == DockerError::Kind::Timeout) {
        return i18n("%1 The registry may be unreachable from the Docker daemon (network, proxy, or IPv6 routing).",
                    base);
    }
    if (mutation == Mutation::BuildImage) {
        // 具体的失败步骤在 detail 里（后端拼好的"Step N/M (命令) failed: …"）
        return i18n("The image could not be built: %1", base);
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
