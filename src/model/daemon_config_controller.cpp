/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/daemon_config_controller.h"

#include "logging.h"

namespace Kontainer
{

namespace
{
/*! `lastError` 里既可能是技术原因，也可能是这个 key（界面据此走降级路径）。 */
constexpr auto kPrivilegeRequired = "privilegeRequired";
} // namespace

DaemonConfigController::DaemonConfigController(QObject *parent)
    : QObject(parent)
{
    refreshFromDisk();
}

void DaemonConfigController::setEngineInfo(const EngineInfo &info)
{
    m_engine = info;
    m_activeMirrors = info.registryMirrors;
    refreshFromDisk();
}

QString DaemonConfigController::formKey() const
{
    return m_deployment.formKey();
}

QString DaemonConfigController::configPath() const
{
    return m_deployment.configPath;
}

bool DaemonConfigController::configExists() const
{
    return m_deployment.configExists;
}

bool DaemonConfigController::configWritable() const
{
    return m_deployment.configWritable;
}

bool DaemonConfigController::requiresPrivilege() const
{
    return m_deployment.requiresPrivilege();
}

bool DaemonConfigController::dataRootInHomeDir() const
{
    return m_deployment.dataRootInHomeDir;
}

QString DaemonConfigController::parseError() const
{
    return m_document.isValid() ? QString() : m_document.errorText();
}

QStringList DaemonConfigController::registryMirrors() const
{
    return m_edits.setRegistryMirrors ? m_edits.registryMirrors : m_document.registryMirrors();
}

QStringList DaemonConfigController::insecureRegistries() const
{
    return m_edits.setInsecureRegistries ? m_edits.insecureRegistries : m_document.insecureRegistries();
}

int DaemonConfigController::maxConcurrentDownloads() const
{
    return m_edits.maxConcurrentDownloads > 0 ? m_edits.maxConcurrentDownloads : m_document.maxConcurrentDownloads();
}

QString DaemonConfigController::logDriver() const
{
    return m_edits.logDriver.isEmpty() ? m_document.logDriver() : m_edits.logDriver;
}

QString DaemonConfigController::dataRoot() const
{
    return m_document.dataRoot();
}

QString DaemonConfigController::configuredStorageDriver() const
{
    return m_document.storageDriver();
}

QStringList DaemonConfigController::unmanagedKeys() const
{
    return m_document.unmanagedKeys();
}

QStringList DaemonConfigController::activeRegistryMirrors() const
{
    return m_activeMirrors;
}

bool DaemonConfigController::restartPending() const
{
    // 待重启 = "配置文件里的加速器" 与 "/info 报告的加速器" 不一致。
    // Docker 对镜像源做归一化（末尾斜杠、顺序），所以这里按集合比较而不是逐个字符串比较。
    QStringList configured = registryMirrors();
    QStringList active = m_activeMirrors;
    configured.sort();
    active.sort();
    return configured != active;
}

bool DaemonConfigController::liveRestoreEnabled() const
{
    return m_engine.liveRestoreEnabled;
}

QStringList DaemonConfigController::backups() const
{
    return m_backups;
}

QString DaemonConfigController::lastError() const
{
    return m_lastError;
}

QString DaemonConfigController::lastBackupPath() const
{
    return m_lastBackupPath;
}

bool DaemonConfigController::dirty() const
{
    return m_dirty;
}

void DaemonConfigController::reload()
{
    refreshFromDisk();
}

void DaemonConfigController::refreshFromDisk()
{
    m_deployment = DaemonDeploymentDetector::detect(m_engine);
    m_document = DaemonConfigDocument::fromFile(m_deployment.configPath);
    m_backups = DaemonConfigWriter::listBackups(m_deployment.configPath);
    // 重新读盘后，编辑状态归零（磁盘值是新的基准）
    m_edits = DaemonConfigEdits();
    setDirty(false);
    Q_EMIT changed();
}

void DaemonConfigController::setRegistryMirrors(const QStringList &mirrors)
{
    m_edits.setRegistryMirrors = true;
    m_edits.registryMirrors = mirrors;
    setDirty(true);
    Q_EMIT changed();
}

void DaemonConfigController::setInsecureRegistries(const QStringList &registries)
{
    m_edits.setInsecureRegistries = true;
    m_edits.insecureRegistries = registries;
    setDirty(true);
    Q_EMIT changed();
}

void DaemonConfigController::setMaxConcurrentDownloads(int value)
{
    m_edits.maxConcurrentDownloads = value;
    setDirty(true);
    Q_EMIT changed();
}

void DaemonConfigController::setLogDriver(const QString &driver)
{
    m_edits.logDriver = driver;
    setDirty(true);
    Q_EMIT changed();
}

DaemonConfigEdits DaemonConfigController::buildEdits() const
{
    return m_edits;
}

QString DaemonConfigController::pendingContentPreview() const
{
    const QByteArray merged = m_document.merged(buildEdits());
    return QString::fromUtf8(merged);
}

bool DaemonConfigController::save()
{
    setLastError(QString());

    if (!m_document.isValid()) {
        setLastError(QStringLiteral("configUnparsable"));
        return false;
    }
    if (m_edits.isEmpty()) {
        setLastError(QStringLiteral("nothingToSave"));
        return false;
    }

    const QByteArray merged = m_document.merged(buildEdits());
    if (merged.isEmpty()) {
        setLastError(QStringLiteral("mergeFailed"));
        return false;
    }

    if (m_deployment.requiresPrivilege()) {
        // 系统级且不可写：交给 5C 的提权 helper；这里先把"需要提权"这一事实告诉界面，
        // 界面据此走授权流程或降级到"自己动手"命令
        setLastError(QString::fromLatin1(kPrivilegeRequired));
        return false;
    }

    QString backupPath;
    const QString error = DaemonConfigWriter::writeAtomically(m_deployment.configPath, merged, &backupPath);
    if (!error.isEmpty()) {
        qCWarning(kontainerModel) << "writing daemon config failed:" << error;
        setLastError(QStringLiteral("writeFailed"));
        return false;
    }

    m_lastBackupPath = backupPath;
    refreshFromDisk();
    Q_EMIT resultChanged();
    Q_EMIT saved();
    return true;
}

bool DaemonConfigController::restoreBackup(const QString &backupPath)
{
    setLastError(QString());

    QString path = backupPath;
    if (path.isEmpty()) {
        const QStringList available = DaemonConfigWriter::listBackups(m_deployment.configPath);
        if (available.isEmpty()) {
            setLastError(QStringLiteral("noBackup"));
            return false;
        }
        path = available.first();
    }

    const QByteArray content = DaemonConfigWriter::readBackup(path);
    if (content.isEmpty()) {
        setLastError(QStringLiteral("backupUnreadable"));
        return false;
    }
    if (m_deployment.requiresPrivilege()) {
        setLastError(QString::fromLatin1(kPrivilegeRequired));
        return false;
    }

    QString newBackup;
    const QString error = DaemonConfigWriter::writeAtomically(m_deployment.configPath, content, &newBackup);
    if (!error.isEmpty()) {
        qCWarning(kontainerModel) << "restoring daemon config failed:" << error;
        setLastError(QStringLiteral("writeFailed"));
        return false;
    }
    m_lastBackupPath = newBackup;
    refreshFromDisk();
    Q_EMIT resultChanged();
    Q_EMIT saved();
    return true;
}

QString DaemonConfigController::privilegedCommand() const
{
    const QByteArray merged = m_document.merged(buildEdits());
    const QString path = m_deployment.configPath.isEmpty() ? QStringLiteral("/etc/docker/daemon.json") : m_deployment.configPath;
    // 用 tee + here-doc：用户复制到终端即可，内容与界面里预览的一致。
    // 注意：这里不生成 `sudo sh -c` 之类的"任意命令"形态，只是一个受限的写入动作。
    return QStringLiteral("sudo tee %1 >/dev/null <<'EOF'\n%2EOF\nsudo systemctl restart docker")
        .arg(path, QString::fromUtf8(merged));
}

void DaemonConfigController::setLastError(const QString &error)
{
    if (m_lastError == error) {
        return;
    }
    m_lastError = error;
    Q_EMIT resultChanged();
}

void DaemonConfigController::setDirty(bool dirty)
{
    if (m_dirty == dirty) {
        return;
    }
    m_dirty = dirty;
    Q_EMIT dirtyChanged();
}

} // namespace Kontainer
