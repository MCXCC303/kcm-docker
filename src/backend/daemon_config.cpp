/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/daemon_config.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>

namespace Kontainer
{

namespace
{
constexpr auto kRegistryMirrors = "registry-mirrors";
constexpr auto kInsecureRegistries = "insecure-registries";
constexpr auto kMaxConcurrentDownloads = "max-concurrent-downloads";
constexpr auto kLogDriver = "log-driver";
constexpr auto kDataRoot = "data-root";
constexpr auto kStorageDriver = "storage-driver";
constexpr auto kBackupMarker = ".kontainer-backup-";

QStringList stringListOf(const QJsonObject &object, const char *key)
{
    QStringList values;
    const QJsonValue value = object.value(QLatin1String(key));
    if (!value.isArray()) {
        return values;
    }
    const QJsonArray array = value.toArray();
    for (const QJsonValue &entry : array) {
        if (entry.isString()) {
            values.append(entry.toString());
        }
    }
    return values;
}
} // namespace

DaemonConfigDocument DaemonConfigDocument::fromFile(const QString &path)
{
    DaemonConfigDocument document;
    document.m_path = path;

    QFile file(path);
    if (!file.exists()) {
        // Missing file is not an error: Docker allows having no config file
        document.m_valid = true;
        return document;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        document.m_exists = true;
        document.m_valid = false;
        document.m_errorText = QStringLiteral("cannot read %1").arg(path);
        return document;
    }
    document.m_exists = true;
    const QByteArray content = file.readAll();
    file.close();

    DaemonConfigDocument parsed = fromContent(content, path);
    parsed.m_exists = true;
    return parsed;
}

DaemonConfigDocument DaemonConfigDocument::fromContent(const QByteArray &content, const QString &path)
{
    DaemonConfigDocument document;
    document.m_path = path;
    document.m_rawContent = content;
    document.m_exists = !content.trimmed().isEmpty();

    QJsonParseError parseError;
    const QJsonDocument parsed = QJsonDocument::fromJson(content, &parseError);
    if (parseError.error != QJsonParseError::NoError || !parsed.isObject()) {
        document.m_valid = false;
        document.m_errorText = parseError.errorString();
        return document;
    }
    document.m_valid = true;
    document.m_root = parsed.object();
    return document;
}

QStringList DaemonConfigDocument::managedKeys()
{
    return {
        QLatin1String(kRegistryMirrors),
        QLatin1String(kInsecureRegistries),
        QLatin1String(kMaxConcurrentDownloads),
        QLatin1String(kLogDriver),
    };
}

QStringList DaemonConfigDocument::registryMirrors() const
{
    return stringListOf(m_root, kRegistryMirrors);
}

QStringList DaemonConfigDocument::insecureRegistries() const
{
    return stringListOf(m_root, kInsecureRegistries);
}

int DaemonConfigDocument::maxConcurrentDownloads() const
{
    const QJsonValue value = m_root.value(QLatin1String(kMaxConcurrentDownloads));
    return value.isDouble() ? value.toInt() : 0;
}

QString DaemonConfigDocument::logDriver() const
{
    const QJsonValue value = m_root.value(QLatin1String(kLogDriver));
    return value.isString() ? value.toString() : QString();
}

QString DaemonConfigDocument::dataRoot() const
{
    const QJsonValue value = m_root.value(QLatin1String(kDataRoot));
    return value.isString() ? value.toString() : QString();
}

QString DaemonConfigDocument::storageDriver() const
{
    const QJsonValue value = m_root.value(QLatin1String(kStorageDriver));
    return value.isString() ? value.toString() : QString();
}

QStringList DaemonConfigDocument::unmanagedKeys() const
{
    const QStringList managed = managedKeys();
    QStringList others;
    for (auto it = m_root.constBegin(); it != m_root.constEnd(); ++it) {
        if (!managed.contains(it.key())) {
            others.append(it.key());
        }
    }
    others.sort();
    return others;
}

QByteArray DaemonConfigDocument::merged(const DaemonConfigEdits &edits) const
{
    if (!m_valid) {
        // Never overwrite a file we cannot parse (ARCH_V5_V8 §2.3 constraint 2)
        return {};
    }

    QJsonObject root = m_root; // unknown keys preserved verbatim
    if (edits.setRegistryMirrors) {
        if (edits.registryMirrors.isEmpty()) {
            root.remove(QLatin1String(kRegistryMirrors));
        } else {
            QJsonArray array;
            for (const QString &mirror : edits.registryMirrors) {
                array.append(mirror);
            }
            root.insert(QLatin1String(kRegistryMirrors), array);
        }
    }
    if (edits.setInsecureRegistries) {
        if (edits.insecureRegistries.isEmpty()) {
            root.remove(QLatin1String(kInsecureRegistries));
        } else {
            QJsonArray array;
            for (const QString &registry : edits.insecureRegistries) {
                array.append(registry);
            }
            root.insert(QLatin1String(kInsecureRegistries), array);
        }
    }
    switch (edits.concurrentDownloadsEdit) {
    case ConfigEdit::Set:
        root.insert(QLatin1String(kMaxConcurrentDownloads), edits.maxConcurrentDownloads);
        break;
    case ConfigEdit::Remove:
        root.remove(QLatin1String(kMaxConcurrentDownloads));
        break;
    case ConfigEdit::Unchanged:
        break;
    }
    switch (edits.logDriverEdit) {
    case ConfigEdit::Set:
        root.insert(QLatin1String(kLogDriver), edits.logDriver);
        break;
    case ConfigEdit::Remove:
        root.remove(QLatin1String(kLogDriver));
        break;
    case ConfigEdit::Unchanged:
        break;
    }

    QByteArray content = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (!content.endsWith('\n')) {
        content.append('\n');
    }
    return content;
}

QString DaemonConfigWriter::backupPrefix()
{
    return QString::fromLatin1(kBackupMarker);
}

QString DaemonConfigWriter::writeAtomically(const QString &path, const QByteArray &content, QString *backupPath)
{
    if (path.isEmpty()) {
        return QStringLiteral("empty path");
    }
    if (content.isEmpty()) {
        return QStringLiteral("refusing to write empty content");
    }

    // Back up before writing: every save must leave a revertible previous version
    if (QFile::exists(path)) {
        const QString stamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss"));
        const QString backup = path + backupPrefix() + stamp;
        if (!QFile::copy(path, backup)) {
            return QStringLiteral("cannot create backup %1").arg(backup);
        }
        if (backupPath) {
            *backupPath = backup;
        }
    }

    // Atomic write: QSaveFile writes a temp file + rename; the original survives a failure
    // Create the directory first: a freshly installed rootless daemon often lacks ~/.config/docker/
    const QString parentDir = QFileInfo(path).absolutePath();
    if (!parentDir.isEmpty() && !QDir().mkpath(parentDir)) {
        return QStringLiteral("cannot create directory %1").arg(parentDir);
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return QStringLiteral("cannot open %1: %2").arg(path, file.errorString());
    }
    if (file.write(content) != content.size()) {
        file.cancelWriting();
        return QStringLiteral("short write to %1").arg(path);
    }
    if (!file.commit()) {
        return QStringLiteral("cannot commit %1: %2").arg(path, file.errorString());
    }
    return {};
}

QStringList DaemonConfigWriter::listBackups(const QString &path)
{
    QStringList backups;
    if (path.isEmpty()) {
        return backups;
    }
    const QFileInfo info(path);
    QDir directory(info.absolutePath());
    const QString filter = info.fileName() + QLatin1Char('*') + QString::fromLatin1(kBackupMarker) + QLatin1Char('*');
    const QStringList entries = directory.entryList({filter}, QDir::Files, QDir::Name | QDir::Reversed);
    for (const QString &entry : entries) {
        backups.append(directory.absoluteFilePath(entry));
    }
    return backups;
}

QByteArray DaemonConfigWriter::readBackup(const QString &backupPath)
{
    QFile file(backupPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray content = file.readAll();
    file.close();
    // Validate before restoring too: a bad backup must not overwrite a good config
    QJsonParseError parseError;
    const QJsonDocument parsed = QJsonDocument::fromJson(content, &parseError);
    if (parseError.error != QJsonParseError::NoError || !parsed.isObject()) {
        return {};
    }
    return content;
}

} // namespace Kontainer
