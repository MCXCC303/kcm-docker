/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QObject>
#include <QVariantList>
#include <QStringList>

namespace Kontainer
{

/*!
 * 命令历史（用户实测 F3：复杂命令希望能快速复用）。
 *
 * 两个来源：
 *  1. **本地记录**：每次成功提交创建请求时记下命令（`~/.config/kontainerrc` 的 `[CommandHistory]`，
 *     上限 20 条，重复的命令提到最前）；
 *  2. **已有容器的命令**：由界面按需喂进来（`mergeExternal()`），只用于候选展示，不写盘—— 
 *     容器可能随时被删除，把它们的命令持久化下来没有意义。
 *
 * 存储形态：一个 JSON 数组（命令本身是多行文本，用 `QStringList` 存会被换行拆散）。
 */
class CommandHistoryStore : public QObject
{
    Q_OBJECT

    /*!
     * 候选列表 `[{command, source}]`（`source` 是 `local` / `container`）。
     *
     * 必须是**属性**（带 NOTIFY）：QML 里 `model: store.entries()` 这种函数调用不建立依赖，
     * Repeater/ComboBox 不会跟着更新（这个坑在日志、网络、数据卷、预设上各踩过一次）。
     */
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
    Q_PROPERTY(bool empty READ empty NOTIFY changed)

public:
    /*! 上限：再多也不方便在下拉里挑。 */
    static constexpr int kMaxEntries = 20;

    /*! `configPath` 为空时用 `QStandardPaths` 的 `kontainerrc`（测试传临时路径）。 */
    explicit CommandHistoryStore(const QString &configPath = {}, QObject *parent = nullptr);

    bool empty() const;
    QVariantList entries() const;
    /*! 纯命令列表（本地记录在前，外部来源在后；已去重）。 */
    QStringList commands() const;

    /*! 记录一条命令：空的不记，重复的提到最前，超出上限丢弃最旧的。 */
    Q_INVOKABLE void record(const QString &command);
    /*! 合并外部来源（已有容器的命令）：不写盘，重复的忽略。 */
    Q_INVOKABLE void mergeExternal(const QStringList &commands);
    /*! 只清掉本地记录（外部来源是临时的，本来就不落盘）。 */
    Q_INVOKABLE void clearLocal();

Q_SIGNALS:
    void changed();

private:
    void load();
    void save() const;
    /*! 重新计算展示用的候选（本地 + 外部，去重）。 */
    void rebuild() const;

    QString m_configPath;
    QStringList m_local;
    QStringList m_external;
    /*! 缓存（`entries()`/`commands()` 是 const，展示列表在这里算一次）。 */
    mutable QStringList m_merged;
};

} // namespace Kontainer
