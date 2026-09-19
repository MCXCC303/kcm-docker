/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/create_container_controller.h"

#include "model/port_binding_rules.h"
#include "model/port_mapping_model.h"

#include "domain/container.h"
#include "model/command_history_store.h"
#include "model/container_detail_controller.h"
#include "model/operation_controller.h"

#include <KLocalizedString>

#include <QDateTime>

namespace Kontainer
{

namespace
{
/*! 步骤 key：插入或调整顺序时不要依赖下标（界面上按钮与校验都用 key）。 */
const QStringList &stepKeyList()
{
    /*
     * 步骤顺序（用户实测反馈）：镜像 → 基础 → 环境与标签 → **交互** → 端口 → 挂载 → 资源 → 总览。
     *
     * 依据："先确定挂载什么、再确定跑什么命令"的直觉，以及"端口属于交互之后才关心的细节"：
     * 交互（命令/入口点/工作目录/用户 + -i/-t）从原来挤在"基础"里的几个字段独立成一步，
     * 端口排到它后面，环境变量排在它前面。步骤用**稳定 key**，因此顺序调整不会影响
     * 校验、总览与界面按钮的对应关系（只影响 stepKeys() 的顺序）。
     */
    static const QStringList keys = {
        QStringLiteral("image"),
        QStringLiteral("basics"),
        QStringLiteral("environment"),
        QStringLiteral("interactive"),
        QStringLiteral("ports"),
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
                                                     ContainerDetailController *containerDetail,
                                                     CommandHistoryStore *commandHistory,
                                                     QObject *parent)
    : QObject(parent)
    , m_operations(operations)
    , m_presets(presets)
    , m_backend(backend)
    , m_containerDetail(containerDetail)
    , m_commandHistory(commandHistory)
{
    Q_ASSERT(m_operations);
    Q_ASSERT(m_presets);
    Q_ASSERT(m_backend);

    connect(m_presets, &MountPresetStore::changed, this, [this] {
        Q_EMIT presetsChanged();
        Q_EMIT changed();
    });
    // 后端数据变了会影响重名与端口冲突的判断；镜像/网络列表还要重铺选择列表
    connect(m_backend, &DockerBackendInterface::containersUpdated, this, &CreateContainerController::changed);
    connect(m_backend, &DockerBackendInterface::imagesUpdated, this, [this] {
        Q_EMIT choiceListsChanged();
        Q_EMIT changed();
    });
    connect(m_backend, &DockerBackendInterface::networksUpdated, this, [this] {
        // 网络列表是异步到的：如果用户还没选网络，就采用第一个（界面上那个下拉显示的就是它，
        // 不能出现"看着选了、实际提交空"）
        if (m_network.isEmpty()) {
            const QVariantList choices = availableNetworks();
            if (!choices.isEmpty()) {
                m_network = choices.first().toMap().value(QStringLiteral("name")).toString();
            }
        }
        Q_EMIT choiceListsChanged();
        Q_EMIT changed();
    });
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

QString CreateContainerController::defaultNetwork() const
{
    const QVariantList choices = availableNetworks();
    return choices.isEmpty() ? QString() : choices.first().toMap().value(QStringLiteral("name")).toString();
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

bool CreateContainerController::openStdin() const
{
    return m_openStdin;
}

bool CreateContainerController::tty() const
{
    return m_tty;
}

bool CreateContainerController::stdinOnce() const
{
    return m_stdinOnce;
}

void CreateContainerController::setOpenStdin(bool value)
{
    if (assignIfDifferent(m_openStdin, value)) {
        touch();
    }
}

void CreateContainerController::setTty(bool value)
{
    if (assignIfDifferent(m_tty, value)) {
        touch();
    }
}

void CreateContainerController::setStdinOnce(bool value)
{
    if (assignIfDifferent(m_stdinOnce, value)) {
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
    m_network = defaultNetwork();
    m_networkAliasesText.clear();
    m_restartPolicy = QStringLiteral("no");
    m_restartMaxRetries = 0;
    m_memoryLimitBytes = 0;
    m_cpus = 0.0;
    m_privileged = false;
    // 交互能力默认开：容器因此能保持运行（用户实测：默认参数下 alpine 会立刻退出）
    m_openStdin = true;
    m_tty = true;
    m_stdinOnce = false;
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

QVariantList CreateContainerController::availableImages() const
{
    QVariantList result;
    const QList<Image> images = m_backend->images();
    for (const Image &image : images) {
        for (const QString &tag : image.repoTags) {
            // 悬空镜像（<none>:<none>）不进选择列表：没法用引用去创建容器
            if (tag.isEmpty() || tag.startsWith(QLatin1String("<none>"))) {
                continue;
            }
            result.append(QVariantMap {
                {QStringLiteral("reference"), tag},
                {QStringLiteral("shortId"), image.shortId()},
            });
        }
    }
    std::sort(result.begin(), result.end(), [](const QVariant &lhs, const QVariant &rhs) {
        return lhs.toMap().value(QStringLiteral("reference")).toString()
            < rhs.toMap().value(QStringLiteral("reference")).toString();
    });
    return result;
}

QVariantList CreateContainerController::availableNetworks() const
{
    QVariantList result;
    const QList<Network> networks = m_backend->networks();
    for (const Network &network : networks) {
        result.append(QVariantMap {
            {QStringLiteral("name"), network.name},
            {QStringLiteral("driver"), network.driver},
        });
    }
    return result;
}

QString CreateContainerController::suggestedName() const
{
    const QString base = m_name.trimmed();
    if (base.isEmpty()) {
        // 还没有名字：按镜像给一个 docker 风格的候选（`alpine:3.19` → `alpine-3-19-4f2a`）。
        // 之前这里直接返回空，界面上「用建议名称」因此永远没反应（用户实测反馈的 ③）。
        QString stem = m_image.trimmed();
        const int slash = stem.lastIndexOf(QLatin1Char('/'));
        if (slash >= 0) {
            stem = stem.mid(slash + 1); // 去掉仓库前缀，留 `alpine:3.19`
        }
        const int colon = stem.indexOf(QLatin1Char(':'));
        if (colon > 0) {
            stem = stem.left(colon);
        }
        stem = stem.toLower();
        QString sanitized;
        for (const QChar &character : std::as_const(stem)) {
            sanitized += (character.isLetterOrNumber() || character == QLatin1Char('_') || character == QLatin1Char('.'))
                ? character
                : QLatin1Char('-');
        }
        if (sanitized.isEmpty()) {
            sanitized = QStringLiteral("container");
        }
        // 后缀用镜像标签/时间的短哈希，避免两次点击拿到同一个名字
        const uint suffix = qHash(m_image + QString::number(QDateTime::currentMSecsSinceEpoch())) & 0xffff;
        QString candidate = QStringLiteral("%1-%2").arg(sanitized, QString::number(suffix, 16).rightJustified(4, QLatin1Char('0')));
        int counter = 2;
        while (m_operations->containerNameTaken(candidate)) {
            candidate = QStringLiteral("%1-%2-%3").arg(sanitized, QString::number(suffix, 16)).arg(counter);
            ++counter;
        }
        return candidate;
    }
    // 已有名字（克隆进来的）：原名 + `-copy`，被占用就继续加序号（§4.5）
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

    // 列表项只有基本信息；命令/入口点/环境/标签/重启策略这些只在 inspect 里，
    // 因此当调用方从容器详情页进来（详情控制器里就是同一个容器）时，把**完整配置**一并克隆过来。
    // 只复制配置，不复制运行时状态（§4.5）
    const bool hasDetail = m_containerDetail && m_containerDetail->hasDetail()
        && m_containerDetail->containerId() == container.id;
    reset(hasDetail ? m_containerDetail->image() : container.image);
    m_name = container.name + QStringLiteral("-copy");
    const QString suggestion = suggestedName();
    if (!suggestion.isEmpty()) {
        m_name = suggestion;
    }

    if (hasDetail) {
        m_commandText = m_containerDetail->command().join(QLatin1Char('\n'));
        m_entrypointText = m_containerDetail->entrypoint().join(QLatin1Char('\n'));
        m_environmentRows.clear();
        for (const QString &entry : m_containerDetail->environment()) {
            const int separator = entry.indexOf(QLatin1Char('='));
            QVariantMap row;
            row.insert(QStringLiteral("key"), separator > 0 ? entry.left(separator) : entry);
            row.insert(QStringLiteral("value"), separator > 0 ? entry.mid(separator + 1) : QString());
            m_environmentRows.append(row);
        }
        m_labelRows.clear();
        if (DetailListModel *labels = m_containerDetail->labels()) {
            // DetailListModel 只按 role 暴露：这里按 LabelRole/ValueRole 读回键值对
            for (int row = 0; row < labels->count(); ++row) {
                const QModelIndex index = labels->index(row, 0);
                m_labelRows.append(QVariantMap {
                    {QStringLiteral("key"), index.data(DetailListModel::LabelRole).toString()},
                    {QStringLiteral("value"), index.data(DetailListModel::ValueRole).toString()},
                });
            }
        }
        m_workingDirectory = m_containerDetail->workingDirectory();
        m_user = m_containerDetail->user();
        m_restartPolicy = m_containerDetail->restartPolicy().isEmpty() ? QStringLiteral("no")
                                                                      : m_containerDetail->restartPolicy();
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

void CreateContainerController::addPortRow(int containerPort, int hostPort, const QString &hostIp, const QString &protocol)
{
    m_portRows.append(QVariantMap {
        {QStringLiteral("containerPort"), containerPort},
        {QStringLiteral("hostPort"), hostPort},
        {QStringLiteral("hostIp"), hostIp},
        {QStringLiteral("protocol"), protocol},
    });
    touch();
}

void CreateContainerController::setPortRow(int row, const QString &field, const QVariant &value)
{
    if (row < 0 || row >= m_portRows.size()) {
        return;
    }
    QVariantMap entry = m_portRows.at(row).toMap();
    if (entry.value(field) == value) {
        return;
    }
    entry.insert(field, value);
    m_portRows[row] = entry;
    touch();
}

void CreateContainerController::removePortRow(int row)
{
    if (row < 0 || row >= m_portRows.size()) {
        return;
    }
    m_portRows.removeAt(row);
    touch();
}

void CreateContainerController::clearPortRows()
{
    if (m_portRows.isEmpty()) {
        return;
    }
    m_portRows.clear();
    touch();
}

void CreateContainerController::addMountRow(const QString &type, const QString &source, const QString &destination, bool readOnly)
{
    m_mountRows.append(QVariantMap {
        {QStringLiteral("type"), type},
        {QStringLiteral("source"), source},
        {QStringLiteral("destination"), destination},
        {QStringLiteral("readOnly"), readOnly},
    });
    touch();
}

void CreateContainerController::setMountRow(int row, const QString &field, const QVariant &value)
{
    if (row < 0 || row >= m_mountRows.size()) {
        return;
    }
    QVariantMap entry = m_mountRows.at(row).toMap();
    if (entry.value(field) == value) {
        return;
    }
    entry.insert(field, value);
    m_mountRows[row] = entry;
    touch();
}

void CreateContainerController::removeMountRow(int row)
{
    if (row < 0 || row >= m_mountRows.size()) {
        return;
    }
    m_mountRows.removeAt(row);
    touch();
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
    // 只读**不**从预设带过来：同一条预设在不同容器里可能一次只读、一次可写，
    // 因此挂载行默认可写，由用户在该行的「只读」开关上决定（用户实测反馈）
    m_mountRows.append(QVariantMap {
        {QStringLiteral("type"), it->type},
        {QStringLiteral("source"), it->source},
        {QStringLiteral("destination"), it->destination},
        {QStringLiteral("readOnly"), false},
    });
    touch();
    return true;
}

void CreateContainerController::addEmptyMount()
{
    addMountRow();
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
    /*
     * 同一个请求里**自己跟自己**冲突也要拦（实测反馈）：
     * 用户填了 8100→80 / 8100→81 / 8100→82，创建请求本身是合法的，
     * 但启动时 Docker 会报 `Bind for 0.0.0.0:8100 failed: port is already allocated`
     * ——同一个宿主端口在一个容器里只能绑一次。
     */
    QList<PortMappingEntry> accepted;
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
        const QString hostIp = row.value(QStringLiteral("hostIp")).toString();

        // 与本次请求里已经接受的行比较（0 = 随机分配，不参与冲突判断）
        if (hostPort != 0) {
            for (const PortMappingEntry &other : accepted) {
                if (other.hostPort == hostPort && PortBindingRules::hostBindingsOverlap(other.hostIp, hostIp)) {
                    return QStringLiteral("portDuplicateInRequest");
                }
            }
        }

        if (m_operations->hostPortInUse(hostIp, hostPort)) {
            return QStringLiteral("portInUse");
        }

        PortMappingEntry parsed;
        parsed.containerPort = containerPort;
        parsed.protocol = row.value(QStringLiteral("protocol")).toString();
        parsed.hostIp = hostIp;
        parsed.hostPort = quint16(hostPort);
        accepted.append(parsed);
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

QString CreateContainerController::stepErrorKeyForStep(const QString &key) const
{
    // 临时切到该步骤求值：校验逻辑只写一份，避免"界面校验"与"诊断校验"分叉
    auto *self = const_cast<CreateContainerController *>(this);
    const int saved = m_stepIndex;
    const int target = int(stepKeyList().indexOf(key));
    if (target >= 0) {
        self->m_stepIndex = target;
    }
    const QString error = validateCurrentStep();
    self->m_stepIndex = saved;
    return error;
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
    if (key == QLatin1String("interactive")) {
        // 交互步骤只收集字段：命令/入口点是自由文本，-i/-t 是开关，没有阻断性校验
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
    // 命令与入口点：填了就要能在总览里核对（用户实测反馈 ⑧）
    if (!m_commandText.trimmed().isEmpty()) {
        add(i18n("Command"), splitLines(m_commandText).join(QLatin1Char(' ')));
    }
    if (!m_entrypointText.trimmed().isEmpty()) {
        add(i18n("Entry point"), splitLines(m_entrypointText).join(QLatin1Char(' ')));
    }
    if (!m_workingDirectory.trimmed().isEmpty()) {
        add(i18n("Working directory"), m_workingDirectory.trimmed());
    }
    if (!m_user.trimmed().isEmpty()) {
        add(i18n("User"), m_user.trimmed());
    }
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
    // 交互能力：两项都开才写一行，单项也如实写出（用户要能核对）
    if (m_openStdin || m_tty) {
        QStringList interactive;
        if (m_openStdin) {
            interactive.append(i18n("Standard input (-i)"));
        }
        if (m_tty) {
            interactive.append(i18n("Terminal (-t)"));
        }
        add(i18n("Interactive"), interactive.join(QStringLiteral(", ")));
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
    request.insert(QStringLiteral("openStdin"), m_openStdin);
    request.insert(QStringLiteral("tty"), m_tty);
    request.insert(QStringLiteral("stdinOnce"), m_stdinOnce);
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

int CreateContainerController::mergeCommandsFromExistingContainers()
{
    if (!m_commandHistory) {
        return 0;
    }
    QStringList commands;
    if (m_containerDetail && m_containerDetail->hasDetail() && !m_containerDetail->command().isEmpty()) {
        commands.append(m_containerDetail->command().join(QLatin1Char(' ')));
    }
    const int before = m_commandHistory->commands().size();
    m_commandHistory->mergeExternal(commands);
    return m_commandHistory->commands().size() - before;
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
    // 成功提交后记下命令（F3：下次可以直接从历史里挑）
    if (m_commandHistory && !m_commandText.trimmed().isEmpty()) {
        m_commandHistory->record(m_commandText);
    }
    return true;
}

} // namespace Kontainer
