/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/mount_preset_store.h"

#include "model/app_config_path.h"

#include "logging.h"

#include <KConfig>
#include <KConfigGroup>
#include <QDateTime>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QVariantMap>

#include <algorithm>

namespace Kontainer
{

namespace
{
constexpr auto kGroup = "MountPresets";
constexpr auto kOrderKey = "Order";
} // namespace

bool MountPreset::matches(const ContainerMountRequest &request) const
{
    return type == request.type && source == request.source && destination == request.destination;
}

MountPresetStore::MountPresetStore(const QString &configPath, QObject *parent)
    : QObject(parent)
    , m_configPath(configPath.isEmpty() ? defaultAppConfigPath() : configPath)
{
    load();
}

int MountPresetStore::count() const
{
    return int(m_presets.size());
}

bool MountPresetStore::empty() const
{
    return m_presets.isEmpty();
}

QList<MountPreset> MountPresetStore::presets() const
{
    // display order = m_order (the saved sequence); favorites are only sorted first for the UI
    QList<MountPreset> result;
    result.reserve(m_presets.size());
    for (const QString &id : m_order) {
        for (const MountPreset &preset : m_presets) {
            if (preset.id == id) {
                result.append(preset);
                break;
            }
        }
    }
    // fallback: ids absent from m_order (should not happen) are appended in insertion order, never dropped
    for (const MountPreset &preset : m_presets) {
        if (!m_order.contains(preset.id)) {
            result.append(preset);
        }
    }

    std::stable_sort(result.begin(), result.end(), [](const MountPreset &lhs, const MountPreset &rhs) {
        if (lhs.favorite != rhs.favorite) {
            return lhs.favorite; // favorites first
        }
        if (lhs.lastUsedAt != rhs.lastUsedAt) {
            // recently used first; never used (invalid time) goes last
            if (!lhs.lastUsedAt.isValid()) {
                return false;
            }
            if (!rhs.lastUsedAt.isValid()) {
                return true;
            }
            return lhs.lastUsedAt > rhs.lastUsedAt;
        }
        return false; // keep the relative m_order sequence
    });
    return result;
}

QVariantList MountPresetStore::summaries() const
{
    QVariantList result;
    const QList<MountPreset> list = presets();
    result.reserve(list.size());
    for (const MountPreset &preset : list) {
        result.append(QVariantMap {
            {QStringLiteral("id"), preset.id},
            // one-line label for the combo box (★ marks favorites); built here so the UI cannot drift
            {QStringLiteral("label"),
             (preset.favorite ? QStringLiteral("★ ") : QString()) + preset.source + QStringLiteral(" → ") + preset.destination},
            {QStringLiteral("source"), preset.source},
            {QStringLiteral("destination"), preset.destination},
            {QStringLiteral("type"), preset.type},
            {QStringLiteral("readOnly"), preset.readOnly},
            {QStringLiteral("note"), preset.note},
            {QStringLiteral("favorite"), preset.favorite},
        });
    }
    return result;
}

QString MountPresetStore::add(const QString &source,
                             const QString &destination,
                             const QString &type,
                             bool readOnly,
                             const QString &note)
{
    const QString sourceError = validateSource(source, type);
    const QString destinationError = validateDestination(destination);
    if (!sourceError.isEmpty() || !destinationError.isEmpty()) {
        return {};
    }

    ContainerMountRequest request;
    request.type = type;
    request.source = source.trimmed();
    request.destination = destination.trimmed();
    for (const MountPreset &preset : m_presets) {
        if (preset.matches(request)) {
            return preset.id; // already present: return its id instead of adding a duplicate
        }
    }

    MountPreset preset;
    preset.id = nextId();
    preset.source = request.source;
    preset.destination = request.destination;
    preset.type = request.type;
    preset.readOnly = readOnly;
    preset.note = note.trimmed();

    m_presets.append(preset);
    m_order.append(preset.id);
    save();
    Q_EMIT changed();
    return preset.id;
}

bool MountPresetStore::remove(const QString &id)
{
    const auto it = std::find_if(m_presets.begin(), m_presets.end(), [&id](const MountPreset &preset) {
        return preset.id == id;
    });
    if (it == m_presets.end()) {
        return false;
    }
    m_presets.erase(it);
    m_order.removeAll(id);
    save();
    Q_EMIT changed();
    return true;
}

bool MountPresetStore::update(const QString &id,
                             const QString &source,
                             const QString &destination,
                             bool readOnly,
                             const QString &note)
{
    const auto it = std::find_if(m_presets.begin(), m_presets.end(), [&id](const MountPreset &preset) {
        return preset.id == id;
    });
    if (it == m_presets.end()) {
        return false;
    }
    if (!validateSource(source, it->type).isEmpty() || !validateDestination(destination).isEmpty()) {
        return false;
    }
    it->source = source.trimmed();
    it->destination = destination.trimmed();
    it->readOnly = readOnly;
    it->note = note.trimmed();
    save();
    Q_EMIT changed();
    return true;
}

bool MountPresetStore::setFavorite(const QString &id, bool favorite)
{
    const auto it = std::find_if(m_presets.begin(), m_presets.end(), [&id](const MountPreset &preset) {
        return preset.id == id;
    });
    if (it == m_presets.end() || it->favorite == favorite) {
        return false;
    }
    it->favorite = favorite;
    save();
    Q_EMIT changed();
    return true;
}

bool MountPresetStore::moveUp(const QString &id)
{
    const int row = m_order.indexOf(id);
    if (row <= 0) {
        return false;
    }
    m_order.move(row, row - 1);
    save();
    Q_EMIT changed();
    return true;
}

bool MountPresetStore::moveDown(const QString &id)
{
    const int row = m_order.indexOf(id);
    if (row < 0 || row >= m_order.size() - 1) {
        return false;
    }
    m_order.move(row, row + 1);
    save();
    Q_EMIT changed();
    return true;
}

void MountPresetStore::noteUsed(const QList<ContainerMountRequest> &mounts)
{
    if (mounts.isEmpty()) {
        return;
    }
    const QDateTime now = QDateTime::currentDateTimeUtc();
    bool touched = false;
    for (const ContainerMountRequest &mount : mounts) {
        if (mount.type == QLatin1String("tmpfs")) {
            continue; // tmpfs has no reusable host location, so it is never a preset
        }
        const auto it = std::find_if(m_presets.begin(), m_presets.end(), [&mount](const MountPreset &preset) {
            return preset.matches(mount);
        });
        if (it == m_presets.end()) {
            MountPreset preset;
            preset.id = nextId();
            preset.source = mount.source;
            preset.destination = mount.destination;
            preset.type = mount.type;
            preset.readOnly = mount.readOnly;
            preset.lastUsedAt = now;
            m_presets.append(preset);
            m_order.append(preset.id);
        } else {
            it->lastUsedAt = now;
            it->readOnly = mount.readOnly;
        }
        touched = true;
    }
    if (!touched) {
        return;
    }
    trimRecents();
    save();
    Q_EMIT changed();
}

QString MountPresetStore::validateSource(const QString &source, const QString &type)
{
    const QString trimmed = source.trimmed();
    if (trimmed.isEmpty()) {
        return QStringLiteral("sourceRequired");
    }
    if (type == QLatin1String("volume")) {
        // same volume-name rule as Docker
        static const QRegularExpression allowed(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.-]*$"));
        if (!allowed.match(trimmed).hasMatch()) {
            return QStringLiteral("volumeNameInvalid");
        }
        return {};
    }
    if (type == QLatin1String("tmpfs")) {
        return {}; // tmpfs has no source
    }
    // bind: must be absolute (Docker reads a relative path as a named volume — a common misconception)
    if (!trimmed.startsWith(QLatin1Char('/'))) {
        return QStringLiteral("sourceNotAbsolute");
    }
    return {};
}

QString MountPresetStore::validateDestination(const QString &destination)
{
    const QString trimmed = destination.trimmed();
    if (trimmed.isEmpty()) {
        return QStringLiteral("destinationRequired");
    }
    if (!trimmed.startsWith(QLatin1Char('/'))) {
        return QStringLiteral("destinationNotAbsolute");
    }
    return {};
}

QString MountPresetStore::nextId() const
{
    // sequential numbers, not random strings: the config file stays readable and hand-editable
    int counter = m_presets.size() + 1;
    QString candidate = QStringLiteral("preset-%1").arg(counter);
    const auto taken = [this](const QString &id) {
        return std::any_of(m_presets.begin(), m_presets.end(), [&id](const MountPreset &preset) {
            return preset.id == id;
        });
    };
    while (taken(candidate)) {
        ++counter;
        candidate = QStringLiteral("preset-%1").arg(counter);
    }
    return candidate;
}

void MountPresetStore::trimRecents()
{
    // evict only the oldest recently-used entry; favorites and never-used presets stay
    while (m_presets.size() > kMaxRecent) {
        int oldestRow = -1;
        QDateTime oldest;
        for (int row = 0; row < m_presets.size(); ++row) {
            const MountPreset &preset = m_presets.at(row);
            if (preset.favorite || !preset.lastUsedAt.isValid()) {
                continue;
            }
            if (oldestRow < 0 || preset.lastUsedAt < oldest) {
                oldestRow = row;
                oldest = preset.lastUsedAt;
            }
        }
        if (oldestRow < 0) {
            break; // nothing left is evictable
        }
        m_order.removeAll(m_presets.at(oldestRow).id);
        m_presets.removeAt(oldestRow);
    }
}

void MountPresetStore::load()
{
    KConfig config(m_configPath, KConfig::SimpleConfig);
    KConfigGroup group = config.group(QString::fromLatin1(kGroup));

    m_order = group.readEntry(QString::fromLatin1(kOrderKey), QStringList());
    m_presets.clear();
    for (const QString &id : group.groupList()) {
        const KConfigGroup entry = group.group(id);
        MountPreset preset;
        preset.id = id;
        preset.source = entry.readEntry(QStringLiteral("Source"), QString());
        preset.destination = entry.readEntry(QStringLiteral("Destination"), QString());
        preset.type = entry.readEntry(QStringLiteral("Type"), QStringLiteral("bind"));
        preset.readOnly = entry.readEntry(QStringLiteral("ReadOnly"), false);
        preset.note = entry.readEntry(QStringLiteral("Note"), QString());
        preset.favorite = entry.readEntry(QStringLiteral("Favorite"), false);
        const QString used = entry.readEntry(QStringLiteral("LastUsedAt"), QString());
        if (!used.isEmpty()) {
            preset.lastUsedAt = QDateTime::fromString(used, Qt::ISODate);
        }
        if (preset.id.isEmpty() || preset.destination.isEmpty()) {
            qCWarning(kontainerModel) << "skipped malformed mount preset entry:" << id;
            continue;
        }
        if (!m_order.contains(preset.id)) {
            m_order.append(preset.id);
        }
        m_presets.append(preset);
    }
}

void MountPresetStore::save() const
{
    KConfig config(m_configPath, KConfig::SimpleConfig);
    KConfigGroup group = config.group(QString::fromLatin1(kGroup));

    // drop stale entries first (deleting a preset must not leave garbage in the config)
    QStringList ids;
    for (const MountPreset &preset : m_presets) {
        ids.append(preset.id);
    }
    for (const QString &existing : group.groupList()) {
        if (!ids.contains(existing)) {
            group.deleteGroup(existing);
        }
    }

    for (const MountPreset &preset : m_presets) {
        KConfigGroup entry = group.group(preset.id);
        entry.writeEntry(QStringLiteral("Source"), preset.source);
        entry.writeEntry(QStringLiteral("Destination"), preset.destination);
        entry.writeEntry(QStringLiteral("Type"), preset.type);
        entry.writeEntry(QStringLiteral("ReadOnly"), preset.readOnly);
        entry.writeEntry(QStringLiteral("Note"), preset.note);
        entry.writeEntry(QStringLiteral("Favorite"), preset.favorite);
        entry.writeEntry(QStringLiteral("LastUsedAt"),
                         preset.lastUsedAt.isValid() ? preset.lastUsedAt.toString(Qt::ISODate) : QString());
    }
    group.writeEntry(QString::fromLatin1(kOrderKey), m_order);
    config.sync();
}

} // namespace Kontainer
