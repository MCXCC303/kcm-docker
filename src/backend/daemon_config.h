/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace Kontainer
{

/*!
 * `daemon.json` 的编辑意图（ARCH_V5_V8 §2.3）。
 *
 * 只描述"我们管理的键"，其余键在合并时**原样保留**——
 * 用户可能配置了 data-root、features、runtimes 等我们不懂的东西，
 * 一个"配置编辑器"把它们弄丢是最不可接受的失败方式。
 */
struct DaemonConfigEdits {
    /*! 是否修改镜像加速器列表。 */
    bool setRegistryMirrors = false;
    QStringList registryMirrors;
    /*! 是否修改不安全仓库列表。 */
    bool setInsecureRegistries = false;
    QStringList insecureRegistries;
    /*! 是否修改并发下载数（<= 0 表示不修改）。 */
    int maxConcurrentDownloads = -1;
    /*! 是否修改日志驱动（空表示不修改）。 */
    QString logDriver;

    /*! 是否什么都没改。 */
    bool isEmpty() const
    {
        return !setRegistryMirrors && !setInsecureRegistries && maxConcurrentDownloads <= 0 && logDriver.isEmpty();
    }
};

/*!
 * `daemon.json` 的读取结果与写回（ARCH_V5_V8 §2.3）。
 *
 * 读写都围绕一个"允许未知字段"的 JSON 文档：
 *  - 读：解析成功 → 可以按白名单键取值；解析失败 → 只读展示错误，**绝不覆写**
 *  - 写：在原始文档上合并白名单键，未知键逐键保留
 */
class DaemonConfigDocument
{
public:
    /*! 从文件读取；文件不存在时返回空文档（`exists=false`）。 */
    static DaemonConfigDocument fromFile(const QString &path);

    /*! 从内存内容构造（提权 helper 与合并逻辑复用同一套解析/合并语义）。 */
    static DaemonConfigDocument fromContent(const QByteArray &content, const QString &path = QString());

    bool exists() const
    {
        return m_exists;
    }
    bool isValid() const
    {
        return m_valid;
    }
    QString errorText() const
    {
        return m_errorText;
    }
    QString path() const
    {
        return m_path;
    }
    QJsonObject root() const
    {
        return m_root;
    }

    /*! 原始文件内容（用于"字节级保留"的对照与备份校验）。 */
    QByteArray rawContent() const
    {
        return m_rawContent;
    }

    /* --- 白名单键的读取 --- */
    QStringList registryMirrors() const;
    QStringList insecureRegistries() const;
    int maxConcurrentDownloads() const;
    QString logDriver() const;
    QString dataRoot() const;
    QString storageDriver() const;
    /*! 我们管理的键之外的键名（界面显示"其他键：N 个（只读）"）。 */
    QStringList unmanagedKeys() const;

    /*! 我们管理的键集合（顺序即界面顺序）。 */
    static QStringList managedKeys();

    /*!
     * 合并编辑意图并序列化。
     *
     * 返回空数组表示合并失败（例如原文档解析失败）。序列化格式：缩进 2 空格 + 末尾换行，
     * 与 Docker 文档示例一致；键顺序由 QJsonObject 决定（JSON 对象无序，语义不受影响）。
     */
    QByteArray merged(const DaemonConfigEdits &edits) const;

private:
    QString m_path;
    bool m_exists = false;
    bool m_valid = false;
    QString m_errorText;
    QJsonObject m_root;
    QByteArray m_rawContent;
};

/*!
 * 写回与备份（ARCH_V5_V8 §2.4 的"原子写入 + 备份"）。
 *
 * 这里只做"用户可写路径"的写入；系统级路径走 helper（helper 内部用同一套合并逻辑）。
 */
class DaemonConfigWriter
{
public:
    /*! 备份文件前缀：`daemon.json.kontainer-backup-<UTC 时间戳>`。 */
    static QString backupPrefix();

    /*!
     * 原子写入：先写同目录临时文件，再 `rename` 覆盖。
     * 写前把现有文件备份（如果存在）。成功返回空字符串，失败返回原因（技术细节，不是 UI 文案）。
     */
    static QString writeAtomically(const QString &path, const QByteArray &content, QString *backupPath = nullptr);

    /*! 列出某个配置文件已有的备份（新的在前）。 */
    static QStringList listBackups(const QString &path);

    /*! 读取某个备份的内容（用于"恢复上一版"）。 */
    static QByteArray readBackup(const QString &backupPath);
};

} // namespace Kontainer
