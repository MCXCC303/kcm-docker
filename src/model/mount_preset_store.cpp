/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/mount_preset_store.h"

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
    , m_configPath(configPath.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/kontainerrc")
                                        : configPath)
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
    // 展示顺序 = m_order（保存下来的顺序），收藏只是让界面把它们排在最前面
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
    // 兜底：m_order 里没有的（理论上不该出现）按插入顺序补在后面，绝不丢数据
    for (const MountPreset &preset : m_presets) {
        if (!m_order.contains(preset.id)) {
            result.append(preset);
        }
    }

    std::stable_sort(result.begin(), result.end(), [](const MountPreset &lhs, const MountPreset &rhs) {
        if (lhs.favorite != rhs.favorite) {
            return lhs.favorite; // 收藏在前
        }
        if (lhs.lastUsedAt != rhs.lastUsedAt) {
            // 最近用过的在前；没用过的（无效时间）排在后面
            if (!lhs.lastUsedAt.isValid()) {
                return false;
            }
            if (!rhs.lastUsedAt.isValid()) {
                return true;
            }
            return lhs.lastUsedAt > rhs.lastUsedAt;
        }
        return false; // 保持 m_order 的相对顺序
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
            return preset.id; // 已存在：返回它的 id，不重复添加
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
            continue; // tmpfs 没有可复用的宿主位置，不作为预设
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
        // 卷名规则与 Docker 一致
        static const QRegularExpression allowed(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.-]*$"));
        if (!allowed.match(trimmed).hasMatch()) {
            return QStringLiteral("volumeNameInvalid");
        }
        return {};
    }
    if (type == QLatin1String("tmpfs")) {
        return {}; // tmpfs 没有来源
    }
    // bind：必须是绝对路径（相对路径在 Docker 里会被当成命名卷，是最常见的误解之一）
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
    // 用递增编号而不是随机串：配置文件用肉眼看得懂，也便于用户自己编辑
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
    // 只淘汰"最近使用"里最旧的，收藏与从未用过的预设不动
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
            break; // 剩下的都不可淘汰
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

    // 先删掉已经不在列表里的旧条目（用户删除预设后配置里不该留下垃圾）
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
