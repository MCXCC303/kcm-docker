/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>

namespace Kontainer
{

/*!
 * Byte stream → complete JSON lines (ARCH_V4 §2.2.1).
 *
 * Docker's `POST /images/create` streams one JSON object per line, and TCP fragmentation has
 * nothing to do with line boundaries: a line may span chunks, a chunk may hold several lines.
 * This class only joins and parses lines — no I/O, no business semantics — so it can be
 * unit-tested without a socket.
 *
 * Malformed lines are not errors: one unreadable line during a pull must not fail the whole
 * pull; callers can observe them via malformedLines() (count only, never content).
 */
class JsonLineReader
{
public:
    /*! Line length cap; longer lines are dropped whole, so a malformed stream cannot eat memory. */
    static constexpr int kMaxLineBytes = 1024 * 1024;

    /*! Feed a chunk; returns the JSON objects that are now complete. */
    QList<QJsonObject> feed(const QByteArray &chunk);
    /*! End of stream: also parse the last line, which may lack a newline. */
    QList<QJsonObject> finish();

    /*! Number of unparsable lines (log counter only). */
    int malformedLines() const
    {
        return m_malformedLines;
    }
    /*! Number of lines dropped as oversized. */
    int droppedLines() const
    {
        return m_droppedLines;
    }
    /*! Bytes of the partial line still buffered. */
    int pendingBytes() const
    {
        return m_buffer.size();
    }

    void reset();

private:
    QList<QJsonObject> takeCompleteLines();
    void consumeLine(const QByteArray &line, QList<QJsonObject> &out);

    QByteArray m_buffer;
    int m_malformedLines = 0;
    int m_droppedLines = 0;
    bool m_droppingOversizedLine = false;
};

} // namespace Kontainer
