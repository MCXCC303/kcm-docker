/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/daemon_config_controller.h"

#include "kauth/privileged_config_request.h"

#include <algorithm>

#include "backend/privileged_client.h"

#include <QFileInfo>
#include <QTimer>
#include "logging.h"

namespace Kontainer
{

namespace
{
/*! `lastError` 里既可能是技术原因，也可能是这个 key（界面据此走降级路径）。 */
constexpr auto kPrivilegeRequired = "privilegeRequired";
/*! 没有提权通路（helper / policy 未安装）：界面据此直接给出可复制的命令。 */
constexpr auto kHelperUnavailable = "helperUnavailable";
/*! 未解锁（受保护作用域）：保存被拒绝。 */
constexpr auto kLocked = "locked";
/*!
 * 解锁后的有效期（秒）。
 *
 * 这个值必须与 polkit 的 keep 窗口一致：`auth_admin_keep` 默认记住 5 分钟，
 * 界面上的倒计时只是把这段时间显示出来；到期后我们主动上锁，让"还要不要继续"
 * 这件事重新变成用户的显式动作（而不是等到保存时才失败）。
 */
constexpr int kUnlockKeepSeconds = 300;

/*!
 * 两份部署信息是否等价（用于"只在真的变了才通知界面"）。
 *
 * 刻意逐字段比较而不是给 DaemonDeployment 加 operator==：那个结构体是给界面用的，
 * 加比较运算符会让"哪些字段影响界面"这件事变得不明显。
 */
bool sameDeployment(const DaemonDeployment &a, const DaemonDeployment &b)
{
    return a.form == b.form && a.systemConfigPath == b.systemConfigPath && a.userConfigPath == b.userConfigPath
        && a.configPath == b.configPath && a.configExists == b.configExists && a.configWritable == b.configWritable
        && a.configSize == b.configSize && a.configModified == b.configModified
        && a.dataRootInHomeDir == b.dataRootInHomeDir;
}
} // namespace

DaemonConfigController::DaemonConfigController(QObject *parent)
    : QObject(parent)
    , m_unlockTimer(new QTimer(this))
{
    m_unlockTimer->setInterval(1000);
    connect(m_unlockTimer, &QTimer::timeout, this, [this] {
        if (!m_unlocked) {
            return;
        }
        if (m_unlockSecondsRemaining <= 1) {
            lock();
            return;
        }
        --m_unlockSecondsRemaining;
        Q_EMIT authorizationChanged();
    });
    refreshFromDisk();
}

void DaemonConfigController::setScope(const QString &scope)
{
    const QString normalized = scope == QLatin1String("user") ? QStringLiteral("user") : QStringLiteral("system");
    if (m_scope == normalized) {
        return;
    }
    m_scope = normalized;
    lock(); // 换作用域必须重新授权：授权是给"那个文件"的
    refreshFromDisk();
}

QString DaemonConfigController::scope() const
{
    return m_scope;
}

bool DaemonConfigController::activeScope() const
{
    return m_activeScope;
}

bool DaemonConfigController::unlocked() const
{
    return m_unlocked;
}

int DaemonConfigController::unlockSecondsRemaining() const
{
    return m_unlocked ? m_unlockSecondsRemaining : 0;
}

bool DaemonConfigController::privilegeAvailable() const
{
    return m_privilegedClient && m_privilegedClient->writeAvailable();
}

void DaemonConfigController::requestUnlock()
{
    if (!m_privilegedClient) {
        setLastError(QString::fromLatin1(kHelperUnavailable));
        return;
    }
    setLastError(QString());
    m_awaitingAuthorize = true;
    m_privilegedClient->requestAuthorization();
}

void DaemonConfigController::lock()
{
    m_unlockTimer->stop();
    m_unlockSecondsRemaining = 0;
    if (!m_unlocked) {
        return;
    }
    m_unlocked = false;
    // 上锁后迟到的授权结果不再接受：用户已经明确表示不要授权了
    m_awaitingAuthorize = false;
    Q_EMIT authorizationChanged();
}

void DaemonConfigController::setPrivilegedClient(PrivilegedClient *client)
{
    m_privilegedClient = client;
    if (!client) {
        return;
    }
    connect(client, &PrivilegedClient::finished, this, [this](PrivilegedClient::Operation operation, bool success, const QString &errorKey) {
        if (operation == PrivilegedClient::Operation::Authorize) {
            if (!m_awaitingAuthorize) {
                // 别的页面发起的授权：与我无关（共享客户端会广播结果）
                return;
            }
            m_awaitingAuthorize = false;
            if (!success) {
                // 取消授权是正常结果：保持锁定，不当作错误横幅（避免噪音）
                if (errorKey != QLatin1String("cancelled")) {
                    setLastError(errorKey);
                }
                return;
            }
            m_unlocked = true;
            m_unlockSecondsRemaining = kUnlockKeepSeconds;
            m_unlockTimer->start();
            setLastError(QString());
            Q_EMIT authorizationChanged();
            return;
        }
        if (operation == PrivilegedClient::Operation::WriteConfig) {
            if (!m_awaitingWrite) {
                return; // 别的页面发起的写入
            }
            m_awaitingWrite = false;
            if (!success) {
                setLastError(errorKey);
                return;
            }
            refreshFromDisk();
            Q_EMIT resultChanged();
            Q_EMIT saved();
            return;
        }
        if (!m_awaitingRestart) {
            return; // 别的页面发起的重启
        }
        m_awaitingRestart = false;
        Q_EMIT restarted(success, errorKey);
    });
}

void DaemonConfigController::setRunningContainerCount(int count)
{
    if (m_runningContainers == count) {
        return;
    }
    m_runningContainers = count;
    Q_EMIT changed();
}

int DaemonConfigController::runningContainers() const
{
    return m_runningContainers;
}

void DaemonConfigController::restartDocker()
{
    if (!m_privilegedClient) {
        Q_EMIT restarted(false, QStringLiteral("helperUnavailable"));
        return;
    }
    m_awaitingRestart = true;
    m_privilegedClient->restartDocker(m_deployment.form != DaemonForm::Rootless);
}

void DaemonConfigController::setEngineInfo(const EngineInfo &info)
{
    m_engine = info;

    // 这里**不能**重新读盘：`setEngineInfo()` 会被状态刷新（自动刷新、容器列表更新）
    // 反复调用，重新读盘会把 `m_edits` 与 dirty 一并清零——用户正在编辑镜像列表或
    // 并发下载数时，界面会突然恢复成磁盘上的旧值（真实反馈的 bug）。
    // 需要重新读盘时走显式的 reload()／保存后／切换作用域。
    const bool mirrorsChanged = m_activeMirrors != info.registryMirrors;
    m_activeMirrors = info.registryMirrors;

    if (refreshDeployment() || mirrorsChanged) {
        Q_EMIT changed();
    }
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
    // 只看当前作用域的文件能不能写（形态判断在 DaemonDeployment 里，单一实现）
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
    // 0 表示"用 daemon 默认"（界面上显示为「默认」）：包括"删掉这个键"的编辑意图
    if (m_edits.concurrentDownloadsEdit == ConfigEdit::Set) {
        return m_edits.maxConcurrentDownloads;
    }
    if (m_edits.concurrentDownloadsEdit == ConfigEdit::Remove) {
        return 0;
    }
    return m_document.maxConcurrentDownloads();
}

QString DaemonConfigController::logDriver() const
{
    if (m_edits.logDriverEdit == ConfigEdit::Set) {
        return m_edits.logDriver;
    }
    if (m_edits.logDriverEdit == ConfigEdit::Remove) {
        return QString();
    }
    return m_document.logDriver();
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

bool DaemonConfigController::refreshDeployment()
{
    const DaemonDeployment previous = m_deployment;
    const bool previousActiveScope = m_activeScope;

    m_deployment = DaemonDeploymentDetector::detect(m_engine);

    // 作用域决定看哪个文件：用户级 ~/.config/docker/daemon.json、系统级 /etc/docker/daemon.json。
    // 这与"哪个 daemon 在读它"是两件事——后者由 activeScope 告诉界面（改了没生效的坑）。
    const QString scopedPath = m_scope == QLatin1String("user") ? m_deployment.userConfigPath : m_deployment.systemConfigPath;
    m_deployment.configPath = scopedPath;
    const QFileInfo scopedInfo(scopedPath);
    m_deployment.configExists = scopedInfo.exists();
    m_deployment.configWritable = DaemonDeploymentDetector::configIsWritable(scopedPath);
    m_deployment.configSize = m_deployment.configExists ? scopedInfo.size() : 0;
    m_deployment.configModified = m_deployment.configExists ? scopedInfo.lastModified() : QDateTime();

    const bool rootlessDaemon = m_deployment.form == DaemonForm::Rootless;
    m_activeScope = (m_scope == QLatin1String("user")) == rootlessDaemon;

    // 周期刷新（引擎/容器列表每次更新）都会走到这里，因此只在**真的变了**的时候通知界面：
    // 一是避免无谓的绑定重算与列表重建（刷新抖动），二是别把用户正在编辑的内容搅乱
    return previousActiveScope != m_activeScope || !sameDeployment(previous, m_deployment);
}

void DaemonConfigController::refreshFromDisk()
{
    refreshDeployment();

    m_document = DaemonConfigDocument::fromFile(m_deployment.configPath);
    m_backups = DaemonConfigWriter::listBackups(m_deployment.configPath);
    m_unlockSecondsRemaining = m_unlocked ? m_unlockSecondsRemaining : 0;
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
    // 0 = 回到默认（删除该键），> 0 = 写入
    m_edits.concurrentDownloadsEdit = value > 0 ? ConfigEdit::Set : ConfigEdit::Remove;
    m_edits.maxConcurrentDownloads = std::max(0, value);
    setDirty(true);
    Q_EMIT changed();
}

void DaemonConfigController::setLogDriver(const QString &driver)
{
    // 空字符串 = 回到默认（删除该键）
    m_edits.logDriverEdit = driver.isEmpty() ? ConfigEdit::Remove : ConfigEdit::Set;
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

    if (requiresPrivilege()) {
        // 受保护作用域：必须已解锁（界面在未解锁时也会禁用保存按钮，这里是兜底）
        if (!m_unlocked) {
            setLastError(QString::fromLatin1(kLocked));
            return false;
        }
        // 交给受限 helper。没有提权通路时明确告知（界面走降级命令），
        // 绝不让用户以为"点了保存就是保存了"
        if (!m_privilegedClient) {
            setLastError(QString::fromLatin1(kHelperUnavailable));
            return false;
        }
        m_awaitingWrite = true;
        m_privilegedClient->writeConfig(buildEdits());
        return false; // 结果经 finished() 异步回来（成功后发 saved()）
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
    if (requiresPrivilege()) {
        // 恢复也属于写系统文件：同样只能走 helper（这里不提供"绕过"的路径）
        setLastError(m_privilegedClient ? QString::fromLatin1(kPrivilegeRequired) : QString::fromLatin1(kHelperUnavailable));
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

QStringList DaemonConfigController::selectableLogDrivers() const
{
    QStringList drivers;
    drivers.append(QString()); // 「默认」：删除 log-driver 键
    drivers.append(PrivilegedConfigRequest::allowedLogDrivers());
    return drivers;
}

QString DaemonConfigController::privilegedCommand() const
{
    const QByteArray merged = m_document.merged(buildEdits());
    const QString path = m_deployment.configPath.isEmpty() ? QStringLiteral("/etc/docker/daemon.json") : m_deployment.configPath;
    // 用 tee + here-doc：用户复制到终端即可，内容与界面里预览的一致。
    // 注意：这里不生成 `sudo sh -c` 之类的"任意命令"形态，只是一个受限的写入动作。
    // 重启那一步要看 daemon 形态：rootless daemon 是用户自己的服务，`systemctl --user`
    // 即可，不需要（也不应该）用 sudo 去动系统服务。
    const QString restart = m_deployment.form == DaemonForm::Rootless
        ? QStringLiteral("systemctl --user restart docker")
        : QStringLiteral("sudo systemctl restart docker");
    return QStringLiteral("sudo tee %1 >/dev/null <<'EOF'\n%2EOF\n%3").arg(path, QString::fromUtf8(merged), restart);
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
