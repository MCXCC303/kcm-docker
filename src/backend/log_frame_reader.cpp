/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/log_frame_reader.h"

namespace Kontainer
{

namespace
{
constexpr int kHeaderBytes = 8;
constexpr char kEscape = '\x1b';

/*!
 * Strip ANSI escape sequences (CSI / OSC / two-character escapes).
 *
 * Removal only, no rendering: §3.1 says the log is plain display with no theming. Parsing must
 * tolerate sequences split across packets: scanning stops at an incomplete sequence and leaves
 * the rest for the next call via `incompleteTail` (see appendText).
 */
QByteArray stripAnsi(const QByteArray &input, QByteArray *incompleteTail)
{
    QByteArray out;
    out.reserve(input.size());
    int i = 0;
    while (i < input.size()) {
        const char ch = input.at(i);
        if (ch != kEscape) {
            out.append(ch);
            ++i;
            continue;
        }

        // Escape sequence: classify it by the second byte
        if (i + 1 >= input.size()) {
            *incompleteTail = input.mid(i); // truncated sequence, keep for the next call
            return out;
        }
        const char kind = input.at(i + 1);
        if (kind == '[') {
            // CSI: parameters run to a final byte in 0x40–0x7e
            int j = i + 2;
            while (j < input.size() && (static_cast<unsigned char>(input.at(j)) < 0x40 || static_cast<unsigned char>(input.at(j)) > 0x7e)) {
                ++j;
            }
            if (j >= input.size()) {
                *incompleteTail = input.mid(i);
                return out;
            }
            i = j + 1;
            continue;
        }
        if (kind == ']') {
            // OSC: runs to BEL or ST (ESC \)
            int j = i + 2;
            while (j < input.size()) {
                if (input.at(j) == '\x07') {
                    break;
                }
                if (input.at(j) == kEscape && j + 1 < input.size() && input.at(j + 1) == '\\') {
                    break;
                }
                ++j;
            }
            if (j >= input.size() || (input.at(j) == kEscape && j + 1 >= input.size())) {
                *incompleteTail = input.mid(i);
                return out;
            }
            i = (input.at(j) == '\x07') ? j + 1 : j + 2;
            continue;
        }
        // Other two-character escapes (ESC c, ESC ( B …): consume at most two bytes
        i += 2;
    }
    return out;
}
} // namespace

LogFrameReader::LogFrameReader(bool tty)
    : m_tty(tty)
{
}

void LogFrameReader::appendText(LogLine::Stream stream, const QByteArray &data, QList<LogLine> *out)
{
    // Unfinished escape sequence from the previous call: parse it together with this data
    QByteArray combined = m_ansiPending + data;
    m_ansiPending.clear();

    QByteArray incomplete;
    const QByteArray clean = stripAnsi(combined, &incomplete);
    if (!incomplete.isEmpty()) {
        // Truncated sequence: keep it for the next call (never emit it as text)
        m_ansiPending = incomplete;
    }

    for (const char ch : clean) {
        if (m_crPending) {
            m_crPending = false;
            if (ch == '\n') {
                closePendingLine(stream, out); // CRLF: a real line end
                continue;
            }
            // Lone \r: overwrite — first emit the current content as a **provisional** line (progress
            // visible immediately), then clear the buffer and let later bytes rewrite it
            emitProvisionalLine(out);
            m_pendingLine.clear();
            m_hasPending = true;
            m_pendingStream = stream;
        }
        if (ch == '\n') {
            closePendingLine(stream, out);
            continue;
        }
        if (ch == '\r') {
            m_crPending = true;
            continue;
        }
        if (!m_hasPending) {
            m_hasPending = true;
            m_pendingStream = stream;
        }
        m_pendingLine.append(ch);
    }
    m_pendingStartsNewLine = false;
}

void LogFrameReader::emitProvisionalLine(QList<LogLine> *out)
{
    if (!m_hasPending) {
        return;
    }
    LogLine line;
    line.stream = m_pendingStream;
    line.text = QString::fromUtf8(m_pendingLine);
    line.complete = false; // provisional: the console must replace its last line, not append
    out->append(line);
}

void LogFrameReader::closePendingLine(LogLine::Stream stream, QList<LogLine> *out)
{
    LogLine line;
    line.stream = m_hasPending ? m_pendingStream : stream;
    line.text = QString::fromUtf8(m_pendingLine);
    line.complete = true;
    out->append(line);
    m_pendingLine.clear();
    m_hasPending = false;
    m_pendingStartsNewLine = true;
}

QList<LogLine> LogFrameReader::feed(const QByteArray &data)
{
    QList<LogLine> lines;

    if (m_tty) {
        appendText(LogLine::Stream::Stdout, data, &lines);
        return lines;
    }

    m_frameBuffer.append(data);
    while (true) {
        if (m_frameBuffer.size() < kHeaderBytes) {
            break;
        }
        const unsigned char streamByte = static_cast<unsigned char>(m_frameBuffer.at(0));
        const quint32 payloadSize = (quint32(static_cast<unsigned char>(m_frameBuffer.at(4))) << 24)
            | (quint32(static_cast<unsigned char>(m_frameBuffer.at(5))) << 16)
            | (quint32(static_cast<unsigned char>(m_frameBuffer.at(6))) << 8)
            | quint32(static_cast<unsigned char>(m_frameBuffer.at(7)));

        // Validity: stream byte must be 1/2 (0 = stdin never occurs here; the rest is malformed)
        if (streamByte != 1 && streamByte != 2) {
            m_discardedBytes += m_frameBuffer.size();
            m_frameBuffer.clear();
            break;
        }
        if (payloadSize > quint32(kMaxFrameBytes)) {
            // A bad length must not make us allocate 4 GB: drop the header and the whole buffer
            m_discardedBytes += m_frameBuffer.size();
            m_frameBuffer.clear();
            break;
        }
        if (quint32(m_frameBuffer.size() - kHeaderBytes) < payloadSize) {
            break; // partial frame: wait for more bytes
        }

        const QByteArray payload = m_frameBuffer.mid(kHeaderBytes, int(payloadSize));
        m_frameBuffer.remove(0, kHeaderBytes + int(payloadSize));

        appendText(streamByte == 2 ? LogLine::Stream::Stderr : LogLine::Stream::Stdout, payload, &lines);
    }
    return lines;
}

QList<LogLine> LogFrameReader::flush()
{
    QList<LogLine> lines;
    if (m_crPending) {
        // Stream ended right after `\r`: the line was overwritten with nothing (typical progress bar)
        m_crPending = false;
        m_pendingLine.clear();
        m_hasPending = true;
    }
    if (!m_ansiPending.isEmpty()) {
        // An unfinished escape sequence is meaningless at end of stream
        m_discardedBytes += m_ansiPending.size();
        m_ansiPending.clear();
    }
    if (m_hasPending) {
        LogLine line;
        line.stream = m_pendingStream;
        line.text = QString::fromUtf8(m_pendingLine);
        line.complete = false;
        lines.append(line);
        m_pendingLine.clear();
        m_hasPending = false;
        m_pendingStartsNewLine = true;
    }
    // A partial frame and unfinished escapes are meaningless at end of stream: count them as discarded
    m_discardedBytes += m_frameBuffer.size();
    m_frameBuffer.clear();
    return lines;
}

} // namespace Kontainer
