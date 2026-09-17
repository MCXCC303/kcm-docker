/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/create_container_controller.h"

#include "domain/container.h"
#include "model/operation_controller.h"

#include <KLocalizedString>

namespace Kontainer
{

namespace
{
/*! 步骤 key：插入或调整顺序时不要依赖下标（界面上按钮与校验都用 key）。 */
const QStringList &stepKeyList()
{
    static const QStringList keys = {
        QStringLiteral("image"),
        QStringLiteral("basics"),
        QStringLiteral("ports"),
        QStringLiteral("environment"),
        QStringLiteral("mounts"),
        QStringLiteral("resources"),
        QStringLiteral("summary"),
    };
    return keys;
}
} // namespace

QStringList CreateContainerController::stepKeys()
{
    return stepKeyList();
}

CreateContainerController::CreateContainerController(OperationController *operations,
                                                     MountPresetStore *presets,
                                                     DockerBackendInterface *backend,
                                                     QObject *parent)
    : QObject(parent)
    , m_operations(operations)
    , m_presets(presets)
    , m_backend(backend)
{
    Q_ASSERT(m_operations);
    Q_ASSERT(m_presets);
    Q_ASSERT(m_backend);

    connect(m_presets, &MountPresetStore::changed, this, [this] {
        Q_EMIT presetsChanged();
        Q_EMIT changed();
    });
    // 后端数据变了（容器/镜像列表刷新）会影响重名与端口冲突的判断，因此也要重算
    connect(m_backend, &DockerBackendInterface::containersUpdated, this, &CreateContainerController::changed);
    connect(m_backend, &DockerBackendInterface::imagesUpdated, this, &CreateContainerController::changed);
    // 创建成功：把 id 交给界面跳转
    connect(m_backend, &DockerBackendInterface::containerCreated, this, [this](const QString &id, const QString &) {
        m_createdContainerId = id;
        Q_EMIT submitted(id);
    });
}

QString CreateContainerController::stepKey() const
{
    return stepKeyList().value(m_stepIndex);
}

int CreateContainerController::stepIndex() const
{
    return m_stepIndex;
}

int CreateContainerController::stepCount() const
{
    return int(stepKeyList().size());
}

bool CreateContainerController::onSummary() const
{
    return stepKey() == QLatin1String("summary");
}

QString CreateContainerController::stepErrorKey() const
{
    return validateCurrentStep();
}

bool CreateContainerController::canAdvance() const
{
    return validateCurrentStep().isEmpty();
}

QString CreateContainerController::name() const
{
    return m_name;
}

QString CreateContainerController::image() const
{
    return m_image;
}

QString CreateContainerController::commandText() const
{
    return m_commandText;
}

QString CreateContainerController::entrypointText() const
{
    return m_entrypointText;
}

QString CreateContainerController::workingDirectory() const
{
    return m_workingDirectory;
}

QString CreateContainerController::user() const
{
    return m_user;
}

QString CreateContainerController::hostname() const
{
    return m_hostname;
}

QString CreateContainerController::network() const
{
    return m_network;
}

QString CreateContainerController::networkAliasesText() const
{
    return m_networkAliasesText;
}

QString CreateContainerController::restartPolicy() const
{
    return m_restartPolicy;
}

int CreateContainerController::restartMaxRetries() const
{
    return m_restartMaxRetries;
}

qint64 CreateContainerController::memoryLimitBytes() const
{
    return m_memoryLimitBytes;
}

double CreateContainerController::cpus() const
{
    return m_cpus;
}

bool CreateContainerController::privileged() const
{
    return m_privileged;
}

bool CreateContainerController::startAfterCreate() const
{
    return m_startAfterCreate;
}

bool CreateContainerController::pullIfMissing() const
{
    return m_pullIfMissing;
}

QVariantList CreateContainerController::portRows() const
{
    return m_portRows;
}

QVariantList CreateContainerController::environmentRows() const
{
    return m_environmentRows;
}

QVariantList CreateContainerController::labelRows() const
{
    return m_labelRows;
}

QVariantList CreateContainerController::mountRows() const
{
    return m_mountRows;
}

QVariantList CreateContainerController::presets() const
{
    return m_presets->summaries();
}

namespace
{
/*! 只在这些真正改变表单内容时才发 changed（避免每敲一个字符就重算整份总览）。 */
template<typename T>
bool assignIfDifferent(T &target, const T &value)
{
    if (target == value) {
        return false;
    }
    target = value;
    return true;
}
} // namespace

void CreateContainerController::touch()
{
    Q_EMIT changed();
}

void CreateContainerController::setName(const QString &value)
{
    if (assignIfDifferent(m_name, value)) {
        touch();
    }
}

void CreateContainerController::setImage(const QString &value)
{
    if (assignIfDifferent(m_image, value)) {
        touch();
    }
}

void CreateContainerController::setCommandText(const QString &value)
{
    if (assignIfDifferent(m_commandText, value)) {
        touch();
    }
}

void CreateContainerController::setEntrypointText(const QString &value)
{
    if (assignIfDifferent(m_entrypointText, value)) {
        touch();
    }
}

void CreateContainerController::setWorkingDirectory(const QString &value)
{
    if (assignIfDifferent(m_workingDirectory, value)) {
        touch();
    }
}

void CreateContainerController::setUser(const QString &value)
{
    if (assignIfDifferent(m_user, value)) {
        touch();
    }
}

void CreateContainerController::setHostname(const QString &value)
{
    if (assignIfDifferent(m_hostname, value)) {
        touch();
    }
}

void CreateContainerController::setNetwork(const QString &value)
{
    if (assignIfDifferent(m_network, value)) {
        touch();
    }
}

void CreateContainerController::setNetworkAliasesText(const QString &value)
{
    if (assignIfDifferent(m_networkAliasesText, value)) {
        touch();
    }
}

void CreateContainerController::setRestartPolicy(const QString &value)
{
    if (assignIfDifferent(m_restartPolicy, value)) {
        touch();
    }
}

void CreateContainerController::setRestartMaxRetries(int value)
{
    if (assignIfDifferent(m_restartMaxRetries, value)) {
        touch();
    }
}

void CreateContainerController::setMemoryLimitBytes(qint64 value)
{
    if (assignIfDifferent(m_memoryLimitBytes, value)) {
        touch();
    }
}

void CreateContainerController::setCpus(double value)
{
    if (assignIfDifferent(m_cpus, value)) {
        touch();
    }
}

void CreateContainerController::setPrivileged(bool value)
{
    if (assignIfDifferent(m_privileged, value)) {
        touch();
    }
}

void CreateContainerController::setStartAfterCreate(bool value)
{
    if (assignIfDifferent(m_startAfterCreate, value)) {
        touch();
    }
}

void CreateContainerController::setPullIfMissing(bool value)
{
    if (assignIfDifferent(m_pullIfMissing, value)) {
        touch();
    }
}

void CreateContainerController::setPortRows(const QVariantList &rows)
{
    if (assignIfDifferent(m_portRows, rows)) {
        touch();
    }
}

void CreateContainerController::setEnvironmentRows(const QVariantList &rows)
{
    if (assignIfDifferent(m_environmentRows, rows)) {
        touch();
    }
}

void CreateContainerController::setLabelRows(const QVariantList &rows)
{
    if (assignIfDifferent(m_labelRows, rows)) {
        touch();
    }
}

void CreateContainerController::setMountRows(const QVariantList &rows)
{
    if (assignIfDifferent(m_mountRows, rows)) {
        touch();
    }
}

void CreateContainerController::reset(const QString &presetImage)
{
    m_stepIndex = 0;
    m_name.clear();
    m_image = presetImage;
    m_commandText.clear();
    m_entrypointText.clear();
    m_workingDirectory.clear();
    m_user.clear();
    m_hostname.clear();
    m_network.clear();
    m_networkAliasesText.clear();
    m_restartPolicy = QStringLiteral("no");
    m_restartMaxRetries = 0;
    m_memoryLimitBytes = 0;
    m_cpus = 0.0;
    m_privileged = false;
    m_startAfterCreate = true;
    m_pullIfMissing = false;
    m_portRows.clear();
    m_environmentRows.clear();
    m_labelRows.clear();
    m_mountRows.clear();
    m_createdContainerId.clear();
    Q_EMIT stepChanged();
    Q_EMIT changed();
}

QString CreateContainerController::suggestedName() const
{
    const QString base = m_name.trimmed();
    if (base.isEmpty()) {
        return {};
    }
    // 默认候选名：原名 + "-copy"，已被占用就继续加序号（克隆时用，§4.5）
    QString candidate = base + QStringLiteral("-copy");
    int counter = 2;
    while (m_operations->containerNameTaken(candidate)) {
        candidate = base + QStringLiteral("-copy%1").arg(counter);
        ++counter;
    }
    return candidate;
}

bool CreateContainerController::prefillFromContainer(const QString &containerId)
{
    const QList<Container> containers = m_backend->containers();
    const auto it = std::find_if(containers.begin(), containers.end(), [&containerId](const Container &container) {
        return container.id == containerId || container.id.startsWith(containerId);
    });
    if (it == containers.end()) {
        return false;
    }
    const Container &container = *it;

    // 列表项只有基本信息；重启策略/挂载/命令这些只在 inspect 里——因此克隆以**当前详情**为准
    // （调用方从容器详情页进入时，详情控制器里已经有一份完整数据）
    reset(container.image);
    m_name = container.name + QStringLiteral("-copy");
    const QString suggestion = suggestedName();
    if (!suggestion.isEmpty()) {
        m_name = suggestion;
    }

    QVariantList ports;
    for (const Port &port : container.ports) {
        if (!port.isPublished()) {
            continue;
        }
        ports.append(QVariantMap {
            {QStringLiteral("hostIp"), port.ip},
            {QStringLiteral("hostPort"), int(port.publicPort)},
            {QStringLiteral("containerPort"), int(port.privatePort)},
            {QStringLiteral("protocol"), port.type},
        });
    }
    m_portRows = ports;

    Q_EMIT stepChanged();
    Q_EMIT changed();
    return true;
}

bool CreateContainerController::nextStep()
{
    if (onSummary()) {
        return false;
    }
    if (!canAdvance()) {
        return false;
    }
    // 名称的最终确认发生在"基础"这一步之后：进入后续步骤前先填好名字，
    // 否则用户会在总览里才发现没名字
    if (stepKey() == QLatin1String("basics") && m_name.trimmed().isEmpty()) {
        const QString suggestion = suggestedName();
        if (!suggestion.isEmpty()) {
            m_name = suggestion;
        }
    }
    ++m_stepIndex;
    Q_EMIT stepChanged();
    Q_EMIT changed();
    return true;
}

void CreateContainerController::previousStep()
{
    if (m_stepIndex == 0) {
        return;
    }
    --m_stepIndex;
    Q_EMIT stepChanged();
    Q_EMIT changed();
}

bool CreateContainerController::goToStep(const QString &key)
{
    const int target = int(stepKeyList().indexOf(key));
    if (target < 0) {
        return false;
    }
    // 不能跳过没填完的步骤：往前跳要逐步校验（总览只能从最后一步进）
    for (int step = m_stepIndex; step < target; ++step) {
        const int saved = m_stepIndex;
        m_stepIndex = step;
        const bool ok = canAdvance();
        m_stepIndex = saved;
        if (!ok) {
            return false;
        }
    }
    if (target == m_stepIndex) {
        return true;
    }
    m_stepIndex = target;
    Q_EMIT stepChanged();
    Q_EMIT changed();
    return true;
}

bool CreateContainerController::addMountFromPreset(const QString &presetId)
{
    const QList<MountPreset> list = m_presets->presets();
    const auto it = std::find_if(list.begin(), list.end(), [&presetId](const MountPreset &preset) {
        return preset.id == presetId;
    });
    if (it == list.end()) {
        return false;
    }
    for (const QVariant &entry : m_mountRows) {
        const QVariantMap row = entry.toMap();
        if (row.value(QStringLiteral("source")).toString() == it->source
            && row.value(QStringLiteral("destination")).toString() == it->destination) {
            return false; // already added
        }
    }
    m_mountRows.append(QVariantMap {
        {QStringLiteral("type"), it->type},
        {QStringLiteral("source"), it->source},
        {QStringLiteral("destination"), it->destination},
        {QStringLiteral("readOnly"), it->readOnly},
    });
    touch();
    return true;
}

void CreateContainerController::addEmptyMount()
{
    m_mountRows.append(QVariantMap {
        {QStringLiteral("type"), QStringLiteral("bind")},
        {QStringLiteral("source"), QString()},
        {QStringLiteral("destination"), QString()},
        {QStringLiteral("readOnly"), false},
    });
    touch();
}

void CreateContainerController::removeMountAt(int row)
{
    if (row < 0 || row >= m_mountRows.size()) {
        return;
    }
    m_mountRows.removeAt(row);
    touch();
}

QString CreateContainerController::validatePorts() const
{
    for (const QVariant &entry : m_portRows) {
        const QVariantMap row = entry.toMap();
        const quint16 containerPort = quint16(row.value(QStringLiteral("containerPort")).toUInt());
        if (containerPort == 0) {
            return QStringLiteral("portRequired");
        }
        const int hostPort = row.value(QStringLiteral("hostPort")).toInt();
        if (hostPort < 0 || hostPort > 65535) {
            return QStringLiteral("portRange");
        }
        if (m_operations->hostPortInUse(row.value(QStringLiteral("hostIp")).toString(), hostPort)) {
            return QStringLiteral("portInUse");
        }
    }
    return {};
}

QString CreateContainerController::validateMounts() const
{
    QStringList destinations;
    for (const QVariant &entry : m_mountRows) {
        const QVariantMap row = entry.toMap();
        const QString destination = row.value(QStringLiteral("destination")).toString();
        const QString pathError = validateContainerPath(destination);
        if (!pathError.isEmpty()) {
            return pathError;
        }
        if (destinations.contains(destination)) {
            return QStringLiteral("destinationDuplicate");
        }
        destinations.append(destination);

        const QString source = row.value(QStringLiteral("source")).toString();
        const QString type = row.value(QStringLiteral("type"), QStringLiteral("bind")).toString();
        // 宿主路径的缺失**只提示不阻断**（Docker 会自己建目录），但格式必须对：
        // bind 要绝对路径、命名卷要合法卷名
        const QString sourceError = MountPresetStore::validateSource(source, type);
        if (!sourceError.isEmpty()) {
            return sourceError;
        }
    }
    return {};
}

QString CreateContainerController::validateCurrentStep() const
{
    const QString key = stepKey();
    if (key == QLatin1String("image")) {
        if (m_image.trimmed().isEmpty()) {
            return QStringLiteral("imageRequired");
        }
        if (!m_pullIfMissing && !m_operations->imageExistsLocally(m_image.trimmed())) {
            return QStringLiteral("imageNotLocal");
        }
        return {};
    }
    if (key == QLatin1String("basics")) {
        const QString nameError = validateContainerName(m_name);
        if (!nameError.isEmpty()) {
            return nameError;
        }
        if (m_operations->containerNameTaken(m_name)) {
            return QStringLiteral("nameInUse");
        }
        return {};
    }
    if (key == QLatin1String("ports")) {
        return validatePorts();
    }
    if (key == QLatin1String("environment")) {
        for (const QVariant &entry : m_environmentRows) {
            const QVariantMap row = entry.toMap();
            const QString key_ = row.value(QStringLiteral("key")).toString();
            if (key_.trimmed().isEmpty()) {
                continue; // 空行会被忽略，不算错误
            }
            const QString keyError = validateEnvironmentKey(key_);
            if (!keyError.isEmpty()) {
                return keyError;
            }
        }
        for (const QVariant &entry : m_labelRows) {
            const QVariantMap row = entry.toMap();
            const QString key_ = row.value(QStringLiteral("key")).toString();
            if (key_.trimmed().isEmpty()) {
                continue;
            }
            const QString keyError = validateEnvironmentKey(key_);
            if (!keyError.isEmpty()) {
                return keyError;
            }
        }
        return {};
    }
    if (key == QLatin1String("mounts")) {
        const QString mountError = validateMounts();
        if (!mountError.isEmpty()) {
            return mountError;
        }
        return {};
    }
    if (key == QLatin1String("resources")) {
        const QString memoryError = validateMemoryLimit(m_memoryLimitBytes);
        if (!memoryError.isEmpty()) {
            return memoryError;
        }
        return validateCpus(m_cpus);
    }
    // 总览：能走到这里说明前面都通过了
    return {};
}

QVariantList CreateContainerController::summary() const
{
    QVariantList rows;
    const auto add = [&rows](const QString &label, const QString &value) {
        if (!value.isEmpty()) {
            rows.append(QVariantMap {{QStringLiteral("label"), label}, {QStringLiteral("value"), value}});
        }
    };

    add(i18n("Image"), m_image);
    add(i18n("Name"), m_name);
    if (!m_network.isEmpty()) {
        add(i18n("Network"), m_network);
    }
    if (m_restartPolicy != QLatin1String("no")) {
        add(i18n("Restart policy"), m_restartPolicy);
    }
    // 端口/挂载/环境只列数量与关键信息：总览是"核对"，不是把整张表单再抄一遍
    if (!m_portRows.isEmpty()) {
        QStringList ports;
        for (const QVariant &entry : m_portRows) {
            const QVariantMap row = entry.toMap();
            const int hostPort = row.value(QStringLiteral("hostPort")).toInt();
            ports.append(QStringLiteral("%1 → %2")
                             .arg(row.value(QStringLiteral("containerPort")).toString(),
                                  hostPort > 0 ? QString::number(hostPort) : i18n("random")));
        }
        add(i18n("Ports"), ports.join(QStringLiteral(", ")));
    }
    if (!m_mountRows.isEmpty()) {
        QStringList mounts;
        for (const QVariant &entry : m_mountRows) {
            const QVariantMap row = entry.toMap();
            mounts.append(QStringLiteral("%1 → %2%3")
                              .arg(row.value(QStringLiteral("source")).toString(),
                                   row.value(QStringLiteral("destination")).toString(),
                                   row.value(QStringLiteral("readOnly")).toBool() ? QStringLiteral(" (ro)") : QString()));
        }
        add(i18n("Mounts"), mounts.join(QStringLiteral(", ")));
    }
    // 环境变量只列**键名**：值可能是密码（§4.4 与四期 §40 的敏感字段处理一致）
    if (!m_environmentRows.isEmpty()) {
        QStringList keys;
        for (const QVariant &entry : m_environmentRows) {
            const QVariantMap row = entry.toMap();
            const QString key = row.value(QStringLiteral("key")).toString();
            if (!key.trimmed().isEmpty()) {
                keys.append(key);
            }
        }
        add(i18n("Environment variables"), keys.join(QStringLiteral(", ")));
    }
    if (!m_labelRows.isEmpty()) {
        add(i18n("Labels"), i18np("%1 label", "%1 labels", m_labelRows.size()));
    }
    if (m_memoryLimitBytes > 0) {
        add(i18n("Memory limit"), QStringLiteral("%1 MiB").arg(m_memoryLimitBytes / (1024 * 1024)));
    }
    if (m_cpus > 0.0) {
        add(i18n("CPU limit"), QString::number(m_cpus));
    }
    if (m_privileged) {
        add(i18n("Privileged"), i18n("Yes (equivalent to root on the host)"));
    }
    add(i18n("After creating"), m_startAfterCreate ? i18n("Start the container") : i18n("Leave it stopped"));
    return rows;
}

QStringList CreateContainerController::splitLines(const QString &text) const
{
    QStringList result;
    const QStringList parts = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty()) {
            result.append(trimmed);
        }
    }
    return result;
}

QVariantMap CreateContainerController::requestMap() const
{
    QVariantMap request;
    request.insert(QStringLiteral("name"), m_name.trimmed());
    request.insert(QStringLiteral("image"), m_image.trimmed());
    // 命令/入口点：按行拆分（与 docker CLI 的写法一致），空行忽略
    request.insert(QStringLiteral("command"), splitLines(m_commandText));
    request.insert(QStringLiteral("entrypoint"), splitLines(m_entrypointText));
    request.insert(QStringLiteral("workingDirectory"), m_workingDirectory.trimmed());
    request.insert(QStringLiteral("user"), m_user.trimmed());
    request.insert(QStringLiteral("hostname"), m_hostname.trimmed());
    request.insert(QStringLiteral("network"), m_network.trimmed());
    QString aliasesText = m_networkAliasesText;
    aliasesText.replace(QLatin1Char(','), QLatin1Char('\n'));
    request.insert(QStringLiteral("networkAliases"), splitLines(aliasesText));
    request.insert(QStringLiteral("restartPolicy"), m_restartPolicy);
    request.insert(QStringLiteral("restartMaxRetries"), m_restartMaxRetries);
    request.insert(QStringLiteral("memoryLimitBytes"), m_memoryLimitBytes);
    request.insert(QStringLiteral("cpus"), m_cpus);
    request.insert(QStringLiteral("privileged"), m_privileged);
    request.insert(QStringLiteral("startAfterCreate"), m_startAfterCreate);

    // 环境变量：`KEY=value` 形式（与 Docker API 一致）
    QStringList environment;
    for (const QVariant &entry : m_environmentRows) {
        const QVariantMap row = entry.toMap();
        const QString key = row.value(QStringLiteral("key")).toString().trimmed();
        if (!key.isEmpty()) {
            environment.append(key + QLatin1Char('=') + row.value(QStringLiteral("value")).toString());
        }
    }
    request.insert(QStringLiteral("environment"), environment);
    request.insert(QStringLiteral("labels"), m_labelRows);
    request.insert(QStringLiteral("ports"), m_portRows);
    request.insert(QStringLiteral("mounts"), m_mountRows);
    return request;
}

bool CreateContainerController::submit()
{
    if (!canAdvance()) {
        return false;
    }
    if (!m_operations->createContainer(requestMap(), m_pullIfMissing)) {
        return false;
    }
    // 创建成功后把本次挂载并入"最近使用"（预设由此自动积累，§4.1）
    QList<ContainerMountRequest> mounts;
    for (const QVariant &entry : m_mountRows) {
        const QVariantMap row = entry.toMap();
        ContainerMountRequest mount;
        mount.type = row.value(QStringLiteral("type"), QStringLiteral("bind")).toString();
        mount.source = row.value(QStringLiteral("source")).toString();
        mount.destination = row.value(QStringLiteral("destination")).toString();
        mount.readOnly = row.value(QStringLiteral("readOnly")).toBool();
        mounts.append(mount);
    }
    m_presets->noteUsed(mounts);
    return true;
}

} // namespace Kontainer
