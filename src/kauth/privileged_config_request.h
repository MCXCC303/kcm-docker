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

    /*! 日志驱动白名单：接受的值只有这些（其余一律拒绝）。 */
    static QStringList allowedLogDrivers();

    /*!
     * 从 KAuth 参数解析编辑意图。
     *
     * 失败时返回 false 并给出原因 key（`unknownKey` / `invalidValue` / `tooLarge` / `noEdits`）。
     * **任何无法识别的键都会导致整请求被拒绝**，而不是被忽略——
     * "忽略未知参数"会让调用方误以为请求生效了。
     */
    static bool fromArguments(const QVariantMap &arguments, PrivilegedConfigRequest *request, QString *errorKey);

    bool isEmpty() const
    {
        return !m_setRegistryMirrors && !m_setInsecureRegistries && m_maxConcurrentDownloads <= 0 && m_logDriver.isEmpty();
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
    int maxConcurrentDownloads() const
    {
        return m_maxConcurrentDownloads;
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
    int m_maxConcurrentDownloads = 0;
    QString m_logDriver;
};

} // namespace Kontainer
