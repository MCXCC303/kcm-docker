/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
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
 * Controller for container logs (ARCH_V5_V8 §3.1.2/§3.1.4).
 *
 * Turns a byte stream into console text and guards three things that break easily:
 *
 *  1. **Bounded**: dual limits on lines and bytes (default 5000 lines / 512 KiB). Logs grow without end
 *     and would eventually drag the KCM down; trimming drops from the **head** and reports how many
 *     lines went away.
 *  2. **Batched**: flush on ~100 ms or 32 KiB accumulated, so every frame does not trigger QML binding
 *     updates and scrolling (§3.1.3).
 *  3. **Pause buffers, never discards**: while paused, lines are still received but not appended to the
 *     visible text and not scrolled; resume appends them all at once, so pausing to read loses nothing.
 *
 * The text is deliberately **one whole string** (for a single read-only monospace TextArea), not one
 * delegate per line: logs append at high frequency and per-line delegates churn items and relayout
 * (§3.1.3).
 */
class ContainerLogController : public QObject
{
    Q_OBJECT

    /*! Console text (already trimmed to the limits). */
    Q_PROPERTY(QString text READ text NOTIFY textChanged)
    /*! Line count (still in the buffer after trimming). */
    Q_PROPERTY(int lineCount READ lineCount NOTIFY textChanged)
    /*! Lines dropped by the limits (the UI uses it to say "older log lines were omitted"). */
    Q_PROPERTY(int droppedLineCount READ droppedLineCount NOTIFY textChanged)
    /*! `idle` / `connecting` / `streaming` / `paused` / `ended` / `failed`. */
    Q_PROPERTY(QString stateKey READ stateKey NOTIFY stateChanged)
    /*! Failure reason key (`containerGone` / `driverUnsupported` / `failed`…); empty on success. */
    Q_PROPERTY(QString errorKey READ errorKey NOTIFY stateChanged)
    /*! Raw engine/transport message, only for "technical details". */
    Q_PROPERTY(QString errorText READ errorText NOTIFY stateChanged)
    /*! Whether paused (receiving continues, content is just not appended to the visible text). */
    Q_PROPERTY(bool paused READ paused NOTIFY pausedChanged)
    /*! Batch interval in ms; 0 = flush immediately (for tests). */
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

    /*! Limits of the bounded buffer (lines / bytes). */
    static constexpr int kMaxLines = 5000;
    static constexpr int kMaxBytes = 512 * 1024;
    /*! Bytes accumulated before flushing at once (without waiting out a batch interval). */
    static constexpr int kFlushBytes = 32 * 1024;

    /*!
     * Start reading a container's logs (called on entering the logs section).
     *
     * `tailLines` is how much history to read first; `tty` comes from container detail (`Config.Tty`).
     * Any stream already on this console is stopped and the text cleared (reconnect = read from tail
     * again, so nothing is printed twice).
     */
    Q_INVOKABLE void connectTo(const QString &containerId, bool tty, int tailLines = 200);
    /*! Leave the section: stop the stream and clear state (text stays until the next connect). */
    Q_INVOKABLE void disconnect();
    /*! Reconnect (clear, then read tail again; used after a container stops). */
    Q_INVOKABLE void reconnect();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    /*! Clear the text (connection state unchanged). */
    Q_INVOKABLE void clear();

Q_SIGNALS:
    void textChanged();
    void stateChanged();
    void pausedChanged();
    void flushIntervalChanged();
    /*! New content was appended (the UI scrolls to the end; not emitted while paused). */
    void appended();

private:
    void handleLines(const QString &id, const QList<LogLine> &lines);
    void handleFinished(const QString &id, DockerBackendInterface::LogStreamEnd end, const DockerError &error);
    void appendLine(const LogLine &line);
    void scheduleFlush();
    /*! Merge pending content into the visible text (not called while paused). */
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

    /*! Visible lines (anything appended after the last flush sits in m_pending). */
    QStringList m_lines;
    /*! Whether each line ends with a newline (provisional lines do not; matters when replacing). */
    QList<bool> m_lineComplete;
    /*! Whether the last line is provisional (a `\r` overwrite or a half line at stream end);
     * the next provisional line replaces it. */
    bool m_lastLineProvisional = false;
    /*! Pending lines (accumulated within the batch window). */
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
