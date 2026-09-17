/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/docker_backend_interface.h"

#include <QObject>
#include <QString>
#include <QStringList>

class QTimer;

namespace Kontainer
{

/*!
 * 容器日志的控制器（ARCH_V5_V8 §3.1.2/§3.1.4）。
 *
 * 它把"字节流"变成"控制台文本"，并守住三件容易出问题的事：
 *
 *  1. **有界**：行数与字节**双上限**（默认 5000 行 / 512 KiB）。日志可以无限增长，
 *     不设上限迟早把 KCM 拖垮；裁剪时从**头部**丢，并如实报告丢了多少行。
 *  2. **批处理**：按 ~100 ms 或累计 32 KiB 合并刷新一次，避免每来一帧都触发
 *     QML 绑定更新与滚动（§3.1.3）。
 *  3. **暂停是"缓冲"而不是"丢弃"**：暂停期间继续接收、但不追加到可见文本也不滚动；
 *     恢复时一次性补上——这样用户暂停去看一段日志时不会漏掉这期间发生的事。
 *
 * 文本形态刻意是**一整块字符串**（给单块只读等宽 TextArea 用），不是"每行一个 delegate"：
 * 日志是高频追加场景，逐行 delegate 会不断创建/销毁条目并触发布局重排（§3.1.3）。
 */
class ContainerLogController : public QObject
{
    Q_OBJECT

    /*! 控制台文本（已按上限裁剪）。 */
    Q_PROPERTY(QString text READ text NOTIFY textChanged)
    /*! 行数（裁剪后仍在缓冲里的）。 */
    Q_PROPERTY(int lineCount READ lineCount NOTIFY textChanged)
    /*! 被上限裁掉的行数（界面据此提示"较早的日志已省略"）。 */
    Q_PROPERTY(int droppedLineCount READ droppedLineCount NOTIFY textChanged)
    /*! `idle` / `connecting` / `streaming` / `paused` / `ended` / `failed`。 */
    Q_PROPERTY(QString stateKey READ stateKey NOTIFY stateChanged)
    /*! 失败原因 key（`containerGone` / `driverUnsupported` / `failed`…）；成功为空。 */
    Q_PROPERTY(QString errorKey READ errorKey NOTIFY stateChanged)
    /*! 引擎/传输层的原始说明，仅用于"技术细节"。 */
    Q_PROPERTY(QString errorText READ errorText NOTIFY stateChanged)
    /*! 是否处于暂停（暂停期间仍在接收，只是不追加到可见文本）。 */
    Q_PROPERTY(bool paused READ paused NOTIFY pausedChanged)
    /*! 批处理间隔（毫秒）；0 = 立即刷新（测试用）。 */
    Q_PROPERTY(int flushIntervalMs READ flushIntervalMs WRITE setFlushIntervalMs NOTIFY flushIntervalChanged)

public:
    explicit ContainerLogController(DockerBackendInterface *backend, QObject *parent = nullptr);

    QString text() const;
    int lineCount() const;
    int droppedLineCount() const;
    QString stateKey() const;
    QString errorKey() const;
    QString errorText() const;
    bool paused() const;
    int flushIntervalMs() const;
    void setFlushIntervalMs(int intervalMs);

    /*! 有界缓冲的上限（行数 / 字节）。 */
    static constexpr int kMaxLines = 5000;
    static constexpr int kMaxBytes = 512 * 1024;
    /*! 累计多少字节就立刻刷新（不必等满一个批处理周期）。 */
    static constexpr int kFlushBytes = 32 * 1024;

    /*!
     * 开始读取某个容器的日志（进入日志分区时调用）。
     *
     * `tailLines` 是首次读取的历史行数；`tty` 来自容器详情（`Config.Tty`）。
     * 会先断开同一控制台上已有的流，并清空文本（重连=重新从 tail 读，避免重复输出）。
     */
    Q_INVOKABLE void connectTo(const QString &containerId, bool tty, int tailLines = 200);
    /*! 离开分区：停止流并清空状态（文本保留，直到下次连接）。 */
    Q_INVOKABLE void disconnect();
    /*! 重新连接（清空后重新读 tail；容器停止后用户点「重新连接」）。 */
    Q_INVOKABLE void reconnect();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    /*! 清空文本（不改变连接状态）。 */
    Q_INVOKABLE void clear();

Q_SIGNALS:
    void textChanged();
    void stateChanged();
    void pausedChanged();
    void flushIntervalChanged();
    /*! 有新内容追加（界面据此决定是否滚到底；暂停时不发）。 */
    void appended();

private:
    void handleLines(const QString &id, const QList<LogLine> &lines);
    void handleFinished(const QString &id, DockerBackendInterface::LogStreamEnd end, const DockerError &error);
    void appendLine(const LogLine &line);
    void scheduleFlush();
    /*! 把待定内容并进可见文本（暂停时不调用）。 */
    void flushNow();
    void trimToLimits();
    void rebuildText();
    void setState(const QString &stateKey, const QString &errorKey = {}, const QString &errorText = {});
    static QString errorKeyFor(const DockerError &error);

    DockerBackendInterface *m_backend = nullptr;
    QTimer *m_flushTimer = nullptr;

    QString m_containerId;
    bool m_tty = false;
    int m_tailLines = 200;
    bool m_connected = false;

    /*! 已可见的行（最后一次刷新之后的追加都在 m_pending 里）。 */
    QStringList m_lines;
    /*! 行尾是否带换行（临时行没有，替换时要注意）。 */
    QList<bool> m_lineComplete;
    /*! 最后一行是否为"临时行"（`\r` 覆盖或流结束的半行）：下一条临时行替换它。 */
    bool m_lastLineProvisional = false;
    /*! 待刷新（批处理窗口内累积）的行。 */
    QStringList m_pending;
    QList<bool> m_pendingComplete;
    int m_pendingBytes = 0;
    qint64 m_bytes = 0;
    int m_droppedLines = 0;

    QString m_text;
    QString m_stateKey = QStringLiteral("idle");
    QString m_errorKey;
    QString m_errorText;
    bool m_paused = false;
    int m_flushIntervalMs = 100;
};

} // namespace Kontainer
