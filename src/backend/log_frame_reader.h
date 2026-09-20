/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QByteArray>
#include <QList>
#include <QMetaType>
#include <QString>

namespace Kontainer
{

/*!
 * One log fragment (ARCH_V5_V8 §3.1.1).
 *
 * `text` has **no trailing newline**; `complete` says whether the line ended, and the console
 * uses it to append or replace the last line (a `\r`-overwritten progress line emits a
 * `complete == false` provisional line that replaces the previous one, so a progress bar
 * neither floods the console nor stays invisible until a newline arrives).
 */
struct LogLine {
    enum class Stream {
        Stdout,
        Stderr,
    };

    Stream stream = Stream::Stdout;
    QString text;
    /*! Terminated by `\n` (false = partial line left at end of stream). */
    bool complete = false;

    bool operator==(const LogLine &other) const
    {
        return stream == other.stream && text == other.text && complete == other.complete;
    }
};

/*!
 * Docker log byte stream → lines (ARCH_V5_V8 §3.1.1).
 *
 * Pure computation: no socket, no UI, so the error-prone details — partial frames, frames split
 * across packets, malformed frames, ANSI, carriage-return overwrites — can be pinned down alone.
 *
 * Two stream shapes (measured; see ARCH_V5_V8 appendix A.4):
 *
 *  - **non-TTY**: each frame is an 8-byte header (1 = stdout / 2 = stderr, 3 reserved bytes,
 *    4-byte big-endian length) plus payload; frames may span packets (keep partial frames buffered)
 *  - **TTY**: no frames at all, just raw bytes (so branch on `Config.Tty`, never always parse frames)
 *
 * Two things real logs always contain:
 *
 *  - **ANSI escapes** (colors): stripped by default — the console does no theming (§3.1 scope)
 *  - **`\r` overwrites** (progress bars): treated as "rewrite the current line", keeping only the
 *    last overwrite so a progress bar cannot flood the console
 */
class LogFrameReader
{
public:
    /*! When `tty` is true, treat input as a raw byte stream (no 8-byte frame header). */
    explicit LogFrameReader(bool tty = false);

    /*! Append socket bytes; returns the lines completed by them. */
    QList<LogLine> feed(const QByteArray &data);

    /*!
     * End of stream: hand over any leftover partial line and clear state.
     *
     * `complete` is false — it really did not end with a newline (the container may show a prompt).
     */
    QList<LogLine> flush();

    /*! Whether TTY (raw) mode is active. */
    bool isTty() const
    {
        return m_tty;
    }

    /*! Bytes dropped as malformed (bad frame header / oversized frame); for self-checks and assertions. */
    qint64 discardedBytes() const
    {
        return m_discardedBytes;
    }

    /*! Payload cap per frame; anything larger is dropped, so one bad length cannot eat memory. */
    static constexpr int kMaxFrameBytes = 1024 * 1024;

private:
    /*! Split UTF-8 text into lines / at `\r` and emit it (ANSI already stripped). */
    void appendText(LogLine::Stream stream, const QByteArray &data, QList<LogLine> *out);
    /*! Emit the pending line as a complete line. */
    void closePendingLine(LogLine::Stream stream, QList<LogLine> *out);
    /*! Emit the pending line as a **provisional** line (for `\r` overwrites and end of stream). */
    void emitProvisionalLine(QList<LogLine> *out);

    bool m_tty = false;
    /*! Bytes of an incomplete frame (non-TTY mode). */
    QByteArray m_frameBuffer;
    /*! Pending line (no `\n` seen yet). */
    QByteArray m_pendingLine;
    LogLine::Stream m_pendingStream = LogLine::Stream::Stdout;
    bool m_hasPending = false;
    /*! Last output ended with `\n` (keeps fragments without a newline on a fresh line). */
    bool m_pendingStartsNewLine = true;
    /*! `\r` just seen; not yet known whether it is a CRLF line end or an overwrite. */
    bool m_crPending = false;
    /*! Truncated ANSI escape sequence, resumed on the next feed. */
    QByteArray m_ansiPending;
    qint64 m_discardedBytes = 0;
};

} // namespace Kontainer

Q_DECLARE_METATYPE(Kontainer::LogLine)
