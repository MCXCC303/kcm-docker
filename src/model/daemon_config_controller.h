/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/daemon_config.h"
#include "backend/daemon_deployment.h"
#include "domain/engine_info.h"

#include <QObject>
#include <QStringList>

class QTimer;

namespace Kontainer
{

class PrivilegedConfigClient;

/*!
 * 运行时配置页的控制器（ARCH_V5_V8 §2.2/§2.3/§2.4）。
 *
 * 职责：
 *  - 探测部署形态与配置文件状态（只读）
 *  - 读取 `daemon.json`（允许未知键）并把白名单字段暴露给界面
 *  - 与 `/info` 的实际值对照，标出「已生效 / 待重启」
 *  - 保存（用户可写路径直接原子写入；系统级路径走 5C 的提权 helper）
 *  - 生成"自己动手"的降级命令（helper 不可用时不让功能变成死胡同）
 *
 * 不负责：QML 文案（界面负责）、提权细节（helper 负责）。
 */
class DaemonConfigController : public QObject
{
    Q_OBJECT

    /* --- 作用域（ARCH_V5_V8 §2.2 修正：按"哪个 daemon 读这个文件"分离） --- */
    /*! `user`（~/.config/docker/daemon.json）或 `system`（/etc/docker/daemon.json）。 */
    Q_PROPERTY(QString scope READ scope NOTIFY changed)
    /*! 这个作用域是否就是正在运行的 daemon 读取的那一个。 */
    Q_PROPERTY(bool activeScope READ activeScope NOTIFY changed)

    /* --- 解锁状态（受保护作用域） --- */
    /*! 是否已通过 polkit 授权（受保护作用域才有意义）。 */
    Q_PROPERTY(bool unlocked READ unlocked NOTIFY authorizationChanged)
    /*! 授权还剩多少秒；0 表示未解锁。 */
    Q_PROPERTY(int unlockSecondsRemaining READ unlockSecondsRemaining NOTIFY authorizationChanged)
    /*! 当前环境是否具备提权通路（helper/policy 已安装）。 */
    Q_PROPERTY(bool privilegeAvailable READ privilegeAvailable NOTIFY changed)

    /* --- 部署与文件状态 --- */
    Q_PROPERTY(QString formKey READ formKey NOTIFY changed)
    Q_PROPERTY(QString configPath READ configPath NOTIFY changed)
    Q_PROPERTY(bool configExists READ configExists NOTIFY changed)
    Q_PROPERTY(bool configWritable READ configWritable NOTIFY changed)
    Q_PROPERTY(bool requiresPrivilege READ requiresPrivilege NOTIFY changed)
    Q_PROPERTY(bool dataRootInHomeDir READ dataRootInHomeDir NOTIFY changed)
    /*! 解析失败时的技术原因；为空表示文件可用（或不存在）。 */
    Q_PROPERTY(QString parseError READ parseError NOTIFY changed)

    /* --- 我们管理的设置 --- */
    Q_PROPERTY(QStringList registryMirrors READ registryMirrors NOTIFY changed)
    Q_PROPERTY(QStringList insecureRegistries READ insecureRegistries NOTIFY changed)
    Q_PROPERTY(int maxConcurrentDownloads READ maxConcurrentDownloads NOTIFY changed)
    Q_PROPERTY(QString logDriver READ logDriver NOTIFY changed)
    /*! `data-root` / `storage-driver`（只读展示）。 */
    Q_PROPERTY(QString dataRoot READ dataRoot NOTIFY changed)
    Q_PROPERTY(QString configuredStorageDriver READ configuredStorageDriver NOTIFY changed)
    /*! 我们不管的键（界面显示"其他键：N 个（只读）"）。 */
    Q_PROPERTY(QStringList unmanagedKeys READ unmanagedKeys NOTIFY changed)

    /* --- 生效状态 --- */
    /*! `/info` 报告的镜像加速器（实际生效值）。 */
    Q_PROPERTY(QStringList activeRegistryMirrors READ activeRegistryMirrors NOTIFY changed)
    /*! 已保存但尚未生效（需要重启 daemon）。 */
    Q_PROPERTY(bool restartPending READ restartPending NOTIFY changed)
    /*! `LiveRestoreEnabled`：为假时重启会停掉运行中的容器。 */
    Q_PROPERTY(bool liveRestoreEnabled READ liveRestoreEnabled NOTIFY changed)
    /*! 当前配置文件的备份列表（新的在前）。 */
    Q_PROPERTY(QStringList backups READ backups NOTIFY changed)

    /* --- 保存结果 --- */
    Q_PROPERTY(QString lastError READ lastError NOTIFY resultChanged)
    Q_PROPERTY(QString lastBackupPath READ lastBackupPath NOTIFY resultChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)

public:
    /*! 默认作用域：跟着正在运行的 daemon 走（rootless → user，系统级 → system）。 */
    explicit DaemonConfigController(QObject *parent = nullptr);

    /*! 引擎信息变化（`/info` 回来）时更新"生效状态"的对照基准。 */
    void setEngineInfo(const EngineInfo &info);

    /*!
     * 注入提权客户端（组合根负责；为空表示当前环境没有提权通路）。
     * 为空时需要提权的保存会直接给出降级命令，而不是静默失败。
     */
    void setPrivilegedClient(PrivilegedConfigClient *client);
    /*! 运行中的容器数（重启影响提示用）。由 StatusController 提供。 */
    void setRunningContainerCount(int count);

    QString formKey() const;
    QString configPath() const;
    bool configExists() const;
    bool configWritable() const;
    bool requiresPrivilege() const;
    bool dataRootInHomeDir() const;
    QString parseError() const;

    QStringList registryMirrors() const;
    QStringList insecureRegistries() const;
    int maxConcurrentDownloads() const;
    QString logDriver() const;
    QString dataRoot() const;
    QString configuredStorageDriver() const;
    QStringList unmanagedKeys() const;

    QStringList activeRegistryMirrors() const;
    bool restartPending() const;
    bool liveRestoreEnabled() const;
    QStringList backups() const;

    QString lastError() const;
    QString lastBackupPath() const;
    bool dirty() const;
    /*! 运行中的容器数（重启确认文案用）。 */
    int runningContainers() const;

    /*! 重新探测 + 重新读文件（页面进入、保存/重启之后调用）。 */
    Q_INVOKABLE void reload();

    /*! 切换作用域（界面按作用域分成两页/两个入口）。 */
    Q_INVOKABLE void setScope(const QString &scope);
    QString scope() const;
    bool activeScope() const;

    /*! 「解锁」：打一次写配置动作的授权（keep 按动作记忆，随后保存与重启不再询问）。 */
    Q_INVOKABLE void requestUnlock();
    /*! 手动上锁（用户主动收起权限）。 */
    Q_INVOKABLE void lock();
    bool unlocked() const;
    int unlockSecondsRemaining() const;
    bool privilegeAvailable() const;

    /* --- 编辑（界面把当前值塞回来；未调用的字段表示不修改） --- */
    Q_INVOKABLE void setRegistryMirrors(const QStringList &mirrors);
    Q_INVOKABLE void setInsecureRegistries(const QStringList &registries);
    Q_INVOKABLE void setMaxConcurrentDownloads(int value);
    Q_INVOKABLE void setLogDriver(const QString &driver);

    /*! 把当前编辑合并进原文档并返回预览（用于确认对话框里展示将写入的内容）。 */
    Q_INVOKABLE QString pendingContentPreview() const;

    /*!
     * 保存。
     *
     * 用户可写路径 → 直接原子写入并返回 true；
     * 需要提权 → 返回 false 并把 `lastError` 设为 `privilegeRequired`（5C 接上 helper 后由 helper 完成）。
     */
    Q_INVOKABLE bool save();

    /*! 恢复某个备份（传入备份文件路径；空字符串表示最近一个）。 */
    Q_INVOKABLE bool restoreBackup(const QString &backupPath = QString());

    /*! helper 不可用时的"自己动手"命令（可直接复制到终端执行）。 */
    /*!
     * 可选日志驱动（含首项空串 = 使用 daemon 默认）。
     *
     * 名单来自 helper 的白名单（`PrivilegedConfigRequest::allowedLogDrivers`），
     * 界面不再自己抄一份：抄一份的下场是界面能选、helper 拒绝。
     */
    Q_INVOKABLE QStringList selectableLogDrivers() const;

    Q_INVOKABLE QString privilegedCommand() const;

    /*! 重启 Docker（系统级走 helper，rootless 走会话 systemd）。 */
    Q_INVOKABLE void restartDocker();
    /*! 运行中的容器数：重启确认文案要用它（"将停止 N 个运行中的容器"）。 */
    Q_PROPERTY(int runningContainers READ runningContainers NOTIFY changed)

Q_SIGNALS:
    void changed();
    /*! 解锁状态或剩余时间变化。 */
    void authorizationChanged();
    void resultChanged();
    void dirtyChanged();
    /*! 保存成功（页面据此提示"待重启生效"或"已写入"）。 */
    void saved();
    /*! 重启结果（页面据此提示；成功时 daemon 会短暂不可用）。 */
    void restarted(bool success, const QString &errorKey);

private:
    void refreshFromDisk();
    DaemonConfigEdits buildEdits() const;
    void setLastError(const QString &error);
    void setDirty(bool dirty);

    EngineInfo m_engine;
    DaemonDeployment m_deployment;
    DaemonConfigDocument m_document;

    /*! 编辑状态：只有被 set* 调用过的字段才会写回。 */
    DaemonConfigEdits m_edits;

    QStringList m_activeMirrors;
    QStringList m_backups;
    QString m_lastError;
    QString m_lastBackupPath;
    bool m_dirty = false;
    PrivilegedConfigClient *m_privilegedClient = nullptr;
    int m_runningContainers = 0;
    /*! 当前作用域（user / system）与"是否就是运行中的 daemon 读的那个文件"。 */
    QString m_scope = QStringLiteral("system");
    bool m_activeScope = true;
    /*! 解锁状态：到期后自动上锁（polkit 的 keep 窗口约 5 分钟）。 */
    bool m_unlocked = false;
    int m_unlockSecondsRemaining = 0;
    QTimer *m_unlockTimer = nullptr;
};

} // namespace Kontainer
