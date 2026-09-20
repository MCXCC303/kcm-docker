/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/create_container_controller.h"

#include "model/host_port_usage.h"
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
/*! Step keys: never rely on indices when inserting or reordering (UI buttons and validation use keys). */
const QStringList &stepKeyList()
{
    /*
     * Step order (user feedback): image -> basics -> environment and labels -> **interactive** -> ports ->
     * mounts -> resources -> summary.
     *
     * Rationale: "decide what to mount, then what to run", plus "ports are a detail you care about after
     * interactive settings": interactive (command/entrypoint/workdir/user + -i/-t) became a step of its
     * own instead of being crammed into "basics", ports moved after it, environment before it. Steps use
     * **stable keys**, so reordering cannot break validation, the summary or the UI buttons (only the
     * order of stepKeys()).
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
    // Backend data feeds the duplicate-name and port-conflict checks; image/network lists also repopulate
    // the choice lists
    connect(m_backend, &DockerBackendInterface::containersUpdated, this, &CreateContainerController::changed);
    connect(m_backend, &DockerBackendInterface::imagesUpdated, this, [this] {
        Q_EMIT choiceListsChanged();
        Q_EMIT changed();
    });
    connect(m_backend, &DockerBackendInterface::networksUpdated, this, [this] {
        // The network list arrives asynchronously: with no network chosen yet, take the first one (that is
        // what the dropdown shows; we must never submit an empty value that looks selected)
        if (m_network.isEmpty()) {
            const QVariantList choices = availableNetworks();
            if (!choices.isEmpty()) {
                m_network = choices.first().toMap().value(QStringLiteral("name")).toString();
            }
        }
        Q_EMIT choiceListsChanged();
        Q_EMIT changed();
    });
    // Created: hand the id to the UI so it can navigate
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
/*! Emit changed only for real form changes (so no keystroke recomputes the whole summary). */
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
    // Interactive on by default so the container stays up (user-tested: alpine exits at once by default)
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
            // Dangling images (<none>:<none>) stay out of the choices: no reference to create from
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
        // No name yet: derive a docker-style candidate from the image (`alpine:3.19` -> `alpine-3-19-4f2a`).
        // This used to return empty, so the UI's "use suggested name" never reacted (user feedback ③).
        QString stem = m_image.trimmed();
        const int slash = stem.lastIndexOf(QLatin1Char('/'));
        if (slash >= 0) {
            stem = stem.mid(slash + 1); // Drop the repository prefix, keep `alpine:3.19`
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
        // Suffix is a short hash of image + time, so two clicks never yield the same name
        const uint suffix = qHash(m_image + QString::number(QDateTime::currentMSecsSinceEpoch())) & 0xffff;
        QString candidate = QStringLiteral("%1-%2").arg(sanitized, QString::number(suffix, 16).rightJustified(4, QLatin1Char('0')));
        int counter = 2;
        while (m_operations->containerNameTaken(candidate)) {
            candidate = QStringLiteral("%1-%2-%3").arg(sanitized, QString::number(suffix, 16)).arg(counter);
            ++counter;
        }
        return candidate;
    }
    // Name already present (cloned in): original + `-copy`, appending a counter while taken (§4.5)
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

    // List entries carry basics only; command/entrypoint/env/labels/restart policy live only in inspect,
    // so when the caller comes from the container detail page (same container in the detail controller) the
    // **full config** is cloned along. Config only, never runtime state (§4.5)
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
            // DetailListModel exposes roles only: read the key/value pairs back via LabelRole/ValueRole
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
    // The name is finalized after the "basics" step: fill it in before advancing, or the user only finds
    // out on the summary that it is missing
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
    // Unfinished steps cannot be skipped: jumping forward validates each one (the summary comes last)
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
    // Read-only is **not** carried over from the preset: the same preset may be read-only in one container
    // and writable in another, so rows default to writable and the row's "read-only" switch decides
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

QVariantMap CreateContainerController::portRowStatus(int row) const
{
    QVariantMap status;
    status.insert(QStringLiteral("errorKey"), QString());
    status.insert(QStringLiteral("holder"), QString());
    status.insert(QStringLiteral("suggestion"), 0);
    status.insert(QStringLiteral("hostPort"), 0);
    if (row < 0 || row >= m_portRows.size()) {
        return status;
    }

    const QVariantMap entry = m_portRows.at(row).toMap();
    const quint16 containerPort = quint16(entry.value(QStringLiteral("containerPort")).toUInt());
    const int hostPort = entry.value(QStringLiteral("hostPort")).toInt();
    const QString hostIp = entry.value(QStringLiteral("hostIp")).toString();

    // Required/range checks come before "conflict" (they are input problems, not occupancy problems)
    if (containerPort == 0) {
        status.insert(QStringLiteral("errorKey"), QStringLiteral("portRequired"));
        return status;
    }
    if (hostPort < 0 || hostPort > 65535) {
        status.insert(QStringLiteral("errorKey"), QStringLiteral("portRange"));
        return status;
    }
    status.insert(QStringLiteral("hostPort"), hostPort);
    if (hostPort == 0) {
        return status; // Random assignment: no conflict, and no suggestion either
    }

    // Host ports already filled by **other rows** of this request (incl. wildcard overlap); suggestions
    // must avoid them too
    QList<int> otherPorts;
    bool duplicate = false;
    for (int other = 0; other < m_portRows.size(); ++other) {
        if (other == row) {
            continue;
        }
        const QVariantMap otherRow = m_portRows.at(other).toMap();
        const int otherPort = otherRow.value(QStringLiteral("hostPort")).toInt();
        if (otherPort <= 0) {
            continue;
        }
        otherPorts.append(otherPort);
        if (otherPort == hostPort && PortBindingRules::hostBindingsOverlap(otherRow.value(QStringLiteral("hostIp")).toString(), hostIp)) {
            duplicate = true;
        }
    }
    if (duplicate) {
        status.insert(QStringLiteral("errorKey"), QStringLiteral("portDuplicateInRequest"));
    } else {
        const QString holder = HostPortUsage::holderFor(m_backend->containers(), hostIp, hostPort);
        if (!holder.isEmpty()) {
            status.insert(QStringLiteral("errorKey"), QStringLiteral("portInUse"));
            status.insert(QStringLiteral("holder"), holder);
        }
    }

    if (!status.value(QStringLiteral("errorKey")).toString().isEmpty()) {
        status.insert(QStringLiteral("suggestion"), HostPortUsage::nextFreePort(m_backend->containers(), hostPort, otherPorts));
    }
    return status;
}

QVariantList CreateContainerController::portRowStatuses() const
{
    QVariantList statuses;
    statuses.reserve(m_portRows.size());
    for (int row = 0; row < m_portRows.size(); ++row) {
        statuses.append(portRowStatus(row));
    }
    return statuses;
}

QString CreateContainerController::validatePorts() const
{
    // Row status is the single source of truth (the inline UI hints use the same logic; see portRowStatuses)
    for (int row = 0; row < m_portRows.size(); ++row) {
        const QString errorKey = portRowStatus(row).value(QStringLiteral("errorKey")).toString();
        if (!errorKey.isEmpty()) {
            return errorKey;
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
        // A missing host path only **warns**, it does not block (Docker creates the directory itself), but
        // the format must be right: bind needs an absolute path, a named volume a valid volume name
        const QString sourceError = MountPresetStore::validateSource(source, type);
        if (!sourceError.isEmpty()) {
            return sourceError;
        }
    }
    return {};
}

QString CreateContainerController::stepErrorKeyForStep(const QString &key) const
{
    // Switch to that step temporarily: validation is written once, so UI and diagnostics cannot diverge
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
        // Interactive only collects fields: command/entrypoint are free text and -i/-t are switches
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
                continue; // Empty rows are ignored, not an error
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
    // Summary: reaching here means every earlier step passed
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
    // Command and entrypoint: once filled in they must be verifiable in the summary (user feedback ⑧)
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
    // Ports/mounts/env list only counts and key facts: the summary verifies, it does not recopy the form
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
    // Env vars list **names only**: values may be passwords (§4.4, consistent with phase four §40)
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
    // Interactive: both on still writes one row, and a single one is reported faithfully (users verify)
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
    // Command/entrypoint: split by line (as the docker CLI does), empty lines ignored
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

    // Environment: `KEY=value` form (as the Docker API expects)
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
    // After a successful create, merge these mounts into "recently used" (presets accumulate, §4.1)
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
    // Record the command after a successful submit (F3: pick it from history next time)
    if (m_commandHistory && !m_commandText.trimmed().isEmpty()) {
        m_commandHistory->record(m_commandText);
    }
    return true;
}

} // namespace Kontainer
