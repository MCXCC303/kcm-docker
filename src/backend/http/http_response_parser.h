/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>

namespace Kontainer
{

/*!
 * Incremental HTTP/1.1 response parser (ARCH_V1 §10: thin wrapper, no external HTTP library).
 *
 * Pure computation, no I/O, no threads: feed it bytes, so unit tests can cover the
 * Content-Length / chunked / connection-close endings and all kinds of malformed input.
 */
class HttpResponseParser
{
public:
    enum class State {
        StatusLine,
        Headers,
        Body,
        ChunkSize,
        ChunkData,
        ChunkTerminator,
        Trailers,
        Complete,
        Failed,
    };

    /*! Append received bytes and parse as far as possible. */
    void feed(const QByteArray &data);
    /*! Peer closed: decide whether the response is complete (for Connection: close responses). */
    void finishInput();
    void reset();

    State state() const
    {
        return m_state;
    }
    bool isComplete() const
    {
        return m_state == State::Complete;
    }
    bool isFailed() const
    {
        return m_state == State::Failed;
    }

    int statusCode() const
    {
        return m_statusCode;
    }
    QString reasonPhrase() const
    {
        return m_reasonPhrase;
    }
    /*! Case-insensitive header lookup; empty when absent. */
    QByteArray header(const QByteArray &name) const;
    /*! Fully decoded body (chunks already merged). */
    QByteArray body() const
    {
        return m_body;
    }
    /*!
     * Take the body delta not yet consumed (ARCH_V4 §2.2.1).
     *
     * body() keeps its whole-body semantics; this only advances the "already taken" offset,
     * for streaming responses (image pull progress). Non-streaming callers never need it.
     */
    QByteArray takeBody();
    QString errorString() const
    {
        return m_errorString;
    }

private:
    enum class BodyMode {
        None,
        ContentLength,
        Chunked,
        UntilEof,
    };

    void parse();
    bool fail(const QString &reason);

    State m_state = State::StatusLine;
    BodyMode m_bodyMode = BodyMode::None;

    QByteArray m_buffer;
    QByteArray m_body;
    /*! Bytes already taken by takeBody() (body() still returns everything). */
    qsizetype m_bodyConsumed = 0;
    QList<QPair<QByteArray, QByteArray>> m_headers;

    qint64 m_expectedBody = 0;
    qint64 m_chunkRemaining = 0;
    int m_statusCode = 0;
    QString m_reasonPhrase;
    QString m_errorString;
};

} // namespace Kontainer
