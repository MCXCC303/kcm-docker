/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/command_history_store.h"

#include <KConfig>
#include <KConfigGroup>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QVariantMap>

namespace Kontainer
{

namespace
{
constexpr auto kGroup = "CommandHistory";
constexpr auto kCommandsKey = "Commands";
} // namespace

CommandHistoryStore::CommandHistoryStore(const QString &configPath, QObject *parent)
    : QObject(parent)
    , m_configPath(configPath.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/kontainerrc")
                                        : configPath)
{
    load();
}

bool CommandHistoryStore::empty() const
{
    return commands().isEmpty();
}

QStringList CommandHistoryStore::commands() const
{
    rebuild();
    return m_merged;
}

QVariantList CommandHistoryStore::entries() const
{
    rebuild();
    QVariantList result;
    result.reserve(m_merged.size());
    for (const QString &command : m_merged) {
        result.append(QVariantMap {
            {QStringLiteral("command"), command},
            {QStringLiteral("source"), m_local.contains(command) ? QStringLiteral("local") : QStringLiteral("container")},
        });
    }
    return result;
}

void CommandHistoryStore::record(const QString &command)
{
    const QString trimmed = command.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    // 重复的提到最前（"最近用过"比"第一次用过"更有用）
    const int existing = m_local.indexOf(trimmed);
    if (existing == 0) {
        return;
    }
    if (existing > 0) {
        m_local.removeAt(existing);
    }
    m_local.prepend(trimmed);
    while (m_local.size() > kMaxEntries) {
        m_local.removeLast();
    }
    save();
    Q_EMIT changed();
}

void CommandHistoryStore::mergeExternal(const QStringList &commands)
{
    bool added = false;
    for (const QString &command : commands) {
        const QString trimmed = command.trimmed();
        if (trimmed.isEmpty() || m_local.contains(trimmed) || m_external.contains(trimmed)) {
            continue;
        }
        m_external.append(trimmed);
        added = true;
    }
    if (added) {
        Q_EMIT changed();
    }
}

void CommandHistoryStore::clearLocal()
{
    if (m_local.isEmpty()) {
        return;
    }
    m_local.clear();
    save();
    Q_EMIT changed();
}

void CommandHistoryStore::rebuild() const
{
    m_merged = m_local;
    for (const QString &command : m_external) {
        if (!m_merged.contains(command)) {
            m_merged.append(command);
        }
    }
}

void CommandHistoryStore::load()
{
    KConfig config(m_configPath, KConfig::SimpleConfig);
    const KConfigGroup group = config.group(QString::fromLatin1(kGroup));
    const QString raw = group.readEntry(QString::fromLatin1(kCommandsKey), QString());
    if (raw.isEmpty()) {
        return;
    }
    // JSON 数组：命令是多行文本，用 QStringList 存会被换行拆散
    const QJsonArray array = QJsonDocument::fromJson(raw.toUtf8()).array();
    for (const QJsonValue &value : array) {
        const QString command = value.toString();
        if (!command.trimmed().isEmpty() && !m_local.contains(command)) {
            m_local.append(command);
        }
    }
}

void CommandHistoryStore::save() const
{
    KConfig config(m_configPath, KConfig::SimpleConfig);
    KConfigGroup group = config.group(QString::fromLatin1(kGroup));
    QJsonArray array;
    for (const QString &command : m_local) {
        array.append(command);
    }
    group.writeEntry(QString::fromLatin1(kCommandsKey), QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
    config.sync();
}

} // namespace Kontainer
