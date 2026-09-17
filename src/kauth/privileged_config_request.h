/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace Kontainer
{

/*!
 * helper 的 D-Bus 名 / KAuth helper id（**单一来源**）。
 *
 * 同一个字符串必须同时出现在四个地方，任何一处不一致都只会在真机上表现为
 * "授权失败"，而看不出是哪一处写错：
 *
 *   - `KAUTH_HELPER_MAIN()` 的第一个参数（helper 侧，决定它 own 哪个总线名）
 *   - `KAuth::Action::setHelperId()`（会话侧，不设它 polkit 后端会直接拒绝执行）
 *   - `<helper>.actions` 里动作名的前缀（策略侧）
 *   - `/usr/share/dbus-1/system.d/<helper>.conf` 的 `allow own`（总线侧）
 *
 * `tst_kauth_wiring` 把这四处钉在一起。
 */
inline constexpr auto kHelperId = "org.kde.kontainer";

/*!
 * 动作 id（必须与 `kauth/org.kde.kontainer.actions` 的段名一致）。
 *
 * 命名只能用**小写字母与数字**（分层用 `.`）：这是官方教程的要求，
 * 而且 KAuth 自带的 kauth-policy-gen 会直接拒绝大写与下划线
 * （`Wrong action syntax`），所以这里不能用 `write_daemon_config` 这类名字。
 *
 * 动作名 → helper 槽名：去掉 helper id 前缀后把 `.` 换成 `_`
 * （`org.kde.kontainer.daemon.save` → `daemon_save`）。
 */
inline constexpr auto kSaveActionName = "org.kde.kontainer.daemon.save";
inline constexpr auto kRestartActionName = "org.kde.kontainer.daemon.restart";

/*!
 * 提权请求的受限语义（ARCH_V5_V8 §2.4）。
 *
 * **这是提权组件的安全边界**：helper 只接受"对白名单键的编辑意图"，
 * 不接受任意 JSON、不接受路径、不接受命令。helper 自己读取目标文件、
 * 自己合并、自己写入——调用方无法让它写出白名单之外的内容。
 *
 * 这个类刻意不依赖 KAuth / QtWidgets / DBus：它既被 helper 使用，
 * 也被单元测试直接使用（不需要 root 就能验证"越权请求会被拒绝"）。
 */
class PrivilegedConfigRequest
{
public:
    /*! 白名单键（与 DaemonConfigDocument 的管理键一致）。 */
    static QStringList allowedKeys();
    /*! 额外允许的参数键（不是 daemon.json 的键，而是请求本身的开关）。 */
    static QStringList allowedControlKeys();

    /*!
     * 允许被**删除**的键（回到 daemon 默认）。
     *
     * 「设成默认值」与「删掉这个键」不是一回事：daemon 自己的默认值会随版本变化，
     * 而且用户文件里那个键可能是他手动写的。所以删除要作为独立意图传进来
     * （control key `remove`，值是键名列表），并且只接受我们管理的键。
     */
    static QStringList removableKeys();

    /*! 日志驱动白名单：接受的值只有这些（其余一律拒绝）。 */
    static QStringList allowedLogDrivers();

    /*!
     * 从 KAuth 参数解析编辑意图。
     *
     * 失败时返回 false 并给出原因 key（`unknownKey` / `invalidValue` / `tooLarge` / `noEdits`
     * / `conflictingKeys`：同一个键既赋值又要求删除）。
     * **任何无法识别的键都会导致整请求被拒绝**，而不是被忽略——
     * "忽略未知参数"会让调用方误以为请求生效了。
     */
    static bool fromArguments(const QVariantMap &arguments, PrivilegedConfigRequest *request, QString *errorKey);

    bool isEmpty() const
    {
        return !m_setRegistryMirrors && !m_setInsecureRegistries && !m_setMaxConcurrentDownloads && !m_setLogDriver
            && m_removeKeys.isEmpty();
    }
    /*!
     * 只做校验、不写文件。
     *
     * 用途：界面上的「解锁」按钮发起一次授权（polkit 的 keep 是按**动作**记的，
     * 因此必须打同一个 action id 才能真正预热后续保存），helper 收到 dryRun 后
     * 校验完请求就返回成功，不碰磁盘。
     */
    bool dryRun() const
    {
        return m_dryRun;
    }
    /*! 要求删除的键（值是我们管理的键名，已校验）。 */
    QStringList removeKeys() const
    {
        return m_removeKeys;
    }
    bool setRegistryMirrors() const
    {
        return m_setRegistryMirrors;
    }
    QStringList registryMirrors() const
    {
        return m_registryMirrors;
    }
    bool setInsecureRegistries() const
    {
        return m_setInsecureRegistries;
    }
    QStringList insecureRegistries() const
    {
        return m_insecureRegistries;
    }
    bool setMaxConcurrentDownloads() const
    {
        return m_setMaxConcurrentDownloads;
    }
    int maxConcurrentDownloads() const
    {
        return m_maxConcurrentDownloads;
    }
    bool setLogDriver() const
    {
        return m_setLogDriver;
    }
    QString logDriver() const
    {
        return m_logDriver;
    }

    /*!
     * 把编辑意图合并到既有文件内容里。
     *
     * 既有内容解析失败时返回空数组（helper 会据此拒绝写入，绝不覆写看不懂的文件）。
     * 未知键按 `DaemonConfigDocument` 的语义逐键保留。
     */
    QByteArray mergeInto(const QByteArray &existingContent) const;

    /*! 请求内容的字节上限（防止把配置写成几百 KB 的垃圾）。 */
    static constexpr int kMaxContentBytes = 64 * 1024;
    /*! 单个列表最多条目数。 */
    static constexpr int kMaxListEntries = 32;

private:
    bool m_setRegistryMirrors = false;
    QStringList m_registryMirrors;
    bool m_setInsecureRegistries = false;
    QStringList m_insecureRegistries;
    bool m_setMaxConcurrentDownloads = false;
    int m_maxConcurrentDownloads = 0;
    bool m_setLogDriver = false;
    QString m_logDriver;
    QStringList m_removeKeys;
    bool m_dryRun = false;
};

} // namespace Kontainer
