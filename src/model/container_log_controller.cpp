/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/container_log_controller.h"

#include "logging.h"

#include <QTimer>

namespace Kontainer
{

namespace
{
/*! Wording the engine uses when the log driver cannot be read (seen with journald / syslog). */
bool looksLikeUnsupportedLogDriver(const QString &detail)
{
    static const QStringList hints = {
        QStringLiteral("does not support reading"),
        QStringLiteral("not supported by the logging driver"),
    };
    const QString lowered = detail.toLower();
    for (const QString &hint : hints) {
        if (lowered.contains(hint)) {
            return true;
        }
    }
    return false;
}
} // namespace

ContainerLogController::ContainerLogController(DockerBackendInterface *backend, QObject *parent)
    : QObject(parent)
    , m_backend(backend)
    , m_flushTimer(new QTimer(this))
{
    Q_ASSERT(m_backend);
    m_flushTimer->setSingleShot(true);
    connect(m_flushTimer, &QTimer::timeout, this, &ContainerLogController::flushNow);

    connect(m_backend, &DockerBackendInterface::containerLogLines, this, &ContainerLogController::handleLines);
    connect(m_backend, &DockerBackendInterface::containerLogsFinished, this, &ContainerLogController::handleFinished);
}

QString ContainerLogController::text() const
{
    return m_text;
}

int ContainerLogController::lineCount() const
{
    return int(m_lines.size());
}

int ContainerLogController::droppedLineCount() const
{
    return m_droppedLines;
}

QString ContainerLogController::stateKey() const
{
    return m_stateKey;
}

QString ContainerLogController::errorKey() const
{
    return m_errorKey;
}

QString ContainerLogController::errorText() const
{
    return m_errorText;
}

bool ContainerLogController::paused() const
{
    return m_paused;
}

int ContainerLogController::flushIntervalMs() const
{
    return m_flushIntervalMs;
}

void ContainerLogController::setFlushIntervalMs(int intervalMs)
{
    const int bounded = qMax(0, intervalMs);
    if (m_flushIntervalMs == bounded) {
        return;
    }
    m_flushIntervalMs = bounded;
    Q_EMIT flushIntervalChanged();
}

QString ContainerLogController::errorKeyFor(const DockerError &error)
{
    switch (error.kind()) {
    case DockerError::Kind::NotFound:
        return QStringLiteral("containerGone");
    case DockerError::Kind::PermissionDenied:
        return QStringLiteral("permissionDenied");
    case DockerError::Kind::DockerUnavailable:
        return QStringLiteral("dockerUnavailable");
    case DockerError::Kind::Timeout:
        return QStringLiteral("timeout");
    case DockerError::Kind::EngineError:
    case DockerError::Kind::HttpError:
        return looksLikeUnsupportedLogDriver(error.detail()) ? QStringLiteral("driverUnsupported") : QStringLiteral("failed");
    default:
        break;
    }
    return QStringLiteral("failed");
}

void ContainerLogController::setState(const QString &stateKey, const QString &errorKey, const QString &errorText)
{
    if (m_stateKey == stateKey && m_errorKey == errorKey && m_errorText == errorText) {
        return;
    }
    m_stateKey = stateKey;
    m_errorKey = errorKey;
    m_errorText = errorText;
    Q_EMIT stateChanged();
}

void ContainerLogController::connectTo(const QString &containerId, bool tty, int tailLines)
{
    if (containerId.isEmpty()) {
        setState(QStringLiteral("failed"), QStringLiteral("containerGone"));
        return;
    }
    // Switch/reconnect: stop the old stream first (its Cancelled end is ignored by handleFinished)
    if (m_connected) {
        m_backend->stopContainerLogs(m_containerId);
    }

    m_containerId = containerId;
    m_tty = tty;
    m_tailLines = tailLines > 0 ? tailLines : 200;
    clear();
    m_connected = true;
    m_paused = false;
    Q_EMIT pausedChanged();
    setState(QStringLiteral("connecting"));

    m_backend->startContainerLogs(m_containerId, m_tty, true, m_tailLines);
}

void ContainerLogController::disconnect()
{
    if (!m_connected) {
        return;
    }
    m_backend->stopContainerLogs(m_containerId);
    m_connected = false;
    m_paused = false;
    Q_EMIT pausedChanged();
    setState(QStringLiteral("idle"));
}

void ContainerLogController::reconnect()
{
    if (m_containerId.isEmpty()) {
        return;
    }
    connectTo(m_containerId, m_tty, m_tailLines);
}

void ContainerLogController::pause()
{
    if (m_paused || !m_connected) {
        return;
    }
    m_paused = true;
    Q_EMIT pausedChanged();
    setState(QStringLiteral("paused"));
}

void ContainerLogController::resume()
{
    if (!m_paused) {
        return;
    }
    m_paused = false;
    Q_EMIT pausedChanged();
    setState(QStringLiteral("streaming"));
    // Append everything accumulated while paused in one go (no log lines lost)
    flushNow();
}

void ContainerLogController::clear()
{
    m_lines.clear();
    m_lineComplete.clear();
    m_pending.clear();
    m_pendingComplete.clear();
    m_pendingBytes = 0;
    m_bytes = 0;
    m_droppedLines = 0;
    m_lastLineProvisional = false;
    m_text.clear();
    Q_EMIT textChanged();
}

void ContainerLogController::appendLine(const LogLine &line)
{
    // The previous provisional line may already be flushed into the visible buffer: pull it back into
    // pending so the replacement logic below faces a single case (else "10%\n50%" half-output appears)
    if (m_pending.isEmpty() && m_lastLineProvisional && !m_lines.isEmpty()) {
        const QString previous = m_lines.takeLast();
        if (!m_lineComplete.isEmpty()) {
            m_lineComplete.removeLast();
        }
        m_bytes -= int(previous.toUtf8().size()) + 1;
        m_pending.prepend(previous);
        m_pendingComplete.prepend(false);
        m_pendingBytes += int(previous.toUtf8().size());
        m_lastLineProvisional = false;
    }

    // Previous line provisional (`\r` overwrite in progress / half line at stream end) -> the new content
    // replaces it rather than adding a row. The new line need **not** be provisional: a finished line
    // (hit `\n`) is that row's final content too, so it must replace the provisional version or
    // "50%\n100% done" half-output remains. Only provisionality is compared, not the source stream:
    // the console does not color stdout/stderr differently (§3.1 scope)
    if (!m_pending.isEmpty() && !m_pendingComplete.at(m_pendingComplete.size() - 1)) {
        m_pendingBytes -= int(m_pending.last().toUtf8().size());
        m_pending.removeLast();
        m_pendingComplete.removeLast();
    }
    m_pending.append(line.text);
    m_pendingComplete.append(line.complete);
    m_pendingBytes += int(line.text.toUtf8().size());
}

void ContainerLogController::handleLines(const QString &id, const QList<LogLine> &lines)
{
    if (!m_connected || id != m_containerId) {
        return; // Content from another container (or a stream already disconnected)
    }
    for (const LogLine &line : lines) {
        appendLine(line);
    }
    if (!lines.isEmpty() && m_stateKey == QLatin1String("connecting")) {
        setState(m_paused ? QStringLiteral("paused") : QStringLiteral("streaming"));
    }
    if (m_pendingBytes >= kFlushBytes) {
        if (m_paused) {
            return; // While paused: accumulate only, append it all on resume
        }
        flushNow();
        return;
    }
    scheduleFlush();
}

void ContainerLogController::scheduleFlush()
{
    if (m_paused) {
        return; // Paused: schedule no flush, content stays in m_pending
    }
    if (m_flushIntervalMs <= 0) {
        flushNow();
        return;
    }
    if (!m_flushTimer->isActive()) {
        m_flushTimer->start(m_flushIntervalMs);
    }
}

void ContainerLogController::flushNow()
{
    m_flushTimer->stop();
    if (m_pending.isEmpty()) {
        return;
    }
    m_lines += m_pending;
    m_lineComplete += m_pendingComplete;
    for (const QString &line : m_pending) {
        m_bytes += int(line.toUtf8().size());
    }
    m_lastLineProvisional = !m_pendingComplete.isEmpty() && !m_pendingComplete.last();
    m_pending.clear();
    m_pendingComplete.clear();
    m_pendingBytes = 0;

    trimToLimits();
    rebuildText();
    Q_EMIT appended();
}

void ContainerLogController::trimToLimits()
{
    // Dual limits: honor both lines and bytes. Drop from the **head** (keep the newest, like tail)
    while (m_lines.size() > kMaxLines || (m_bytes > kMaxBytes && m_lines.size() > 1) || m_lineComplete.size() > kMaxLines) {
        m_bytes -= int(m_lines.first().toUtf8().size()) + 1;
        m_lines.removeFirst();
        if (!m_lineComplete.isEmpty()) {
            m_lineComplete.removeFirst();
        }
        ++m_droppedLines;
    }
}

void ContainerLogController::rebuildText()
{
    QStringList withNewlines;
    withNewlines.reserve(m_lines.size());
    for (int i = 0; i < m_lines.size(); ++i) {
        const bool complete = i < m_lineComplete.size() ? m_lineComplete.at(i) : true;
        const bool isLast = i == m_lines.size() - 1;
        // If the last line is provisional (`\r` overwrite / half line at stream end), add no newline yet
        withNewlines.append(complete || !isLast ? m_lines.at(i) + QLatin1Char('\n') : m_lines.at(i));
    }
    const QString rebuilt = withNewlines.join(QString());
    if (rebuilt == m_text) {
        return;
    }
    m_text = rebuilt;
    Q_EMIT textChanged();
}

void ContainerLogController::handleFinished(const QString &id, DockerBackendInterface::LogStreamEnd end, const DockerError &error)
{
    if (id != m_containerId) {
        return;
    }
    if (end == DockerBackendInterface::LogStreamEnd::Cancelled) {
        // Stopped by us (container switch / leaving the section / reconnect): not a user-visible error
        return;
    }
    m_connected = false;
    if (end == DockerBackendInterface::LogStreamEnd::Failed) {
        qCWarning(kontainerModel) << "container log stream failed for" << id << error.detail();
        setState(QStringLiteral("failed"), errorKeyFor(error), error.detail());
        return;
    }
    setState(QStringLiteral("ended"));
}

} // namespace Kontainer
