/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/http/http_response_parser.h"

namespace Kontainer
{

namespace
{
constexpr auto crlf = "\r\n";
constexpr int crlfSize = 2;
}

void HttpResponseParser::reset()
{
    m_state = State::StatusLine;
    m_bodyMode = BodyMode::None;
    m_buffer.clear();
    m_body.clear();
    m_bodyConsumed = 0;
    m_headers.clear();
    m_expectedBody = 0;
    m_chunkRemaining = 0;
    m_statusCode = 0;
    m_reasonPhrase.clear();
    m_errorString.clear();
}

QByteArray HttpResponseParser::takeBody()
{
    if (m_bodyConsumed >= m_body.size()) {
        return {};
    }
    const QByteArray delta = m_body.mid(m_bodyConsumed);
    m_bodyConsumed = m_body.size();
    return delta;
}

void HttpResponseParser::feed(const QByteArray &data)
{
    if (isComplete() || isFailed()) {
        return;
    }
    m_buffer.append(data);
    parse();
}

void HttpResponseParser::finishInput()
{
    if (isComplete() || isFailed()) {
        return;
    }
    parse();
    if (isComplete() || isFailed()) {
        return;
    }
    if (m_state == State::Body && m_bodyMode == BodyMode::UntilEof) {
        m_body.append(m_buffer);
        m_buffer.clear();
        m_state = State::Complete;
        return;
    }
    fail(QStringLiteral("connection closed before the response was complete"));
}

QByteArray HttpResponseParser::header(const QByteArray &name) const
{
    const QByteArray lowered = name.toLower();
    for (const auto &[key, value] : m_headers) {
        if (key == lowered) {
            return value;
        }
    }
    return {};
}

bool HttpResponseParser::fail(const QString &reason)
{
    m_state = State::Failed;
    m_errorString = reason;
    m_buffer.clear();
    return false;
}

void HttpResponseParser::parse()
{
    while (!isComplete() && !isFailed()) {
        switch (m_state) {
        case State::StatusLine: {
            const int eol = m_buffer.indexOf(crlf);
            if (eol < 0) {
                return;
            }
            const QByteArray line = m_buffer.left(eol);
            m_buffer.remove(0, eol + crlfSize);

            const QList<QByteArray> parts = line.split(' ');
            if (parts.size() < 2 || !parts.at(0).startsWith("HTTP/")) {
                fail(QStringLiteral("invalid HTTP status line"));
                return;
            }
            bool ok = false;
            const int code = parts.at(1).toInt(&ok);
            if (!ok || code < 100 || code > 599) {
                fail(QStringLiteral("invalid HTTP status code"));
                return;
            }
            m_statusCode = code;
            m_reasonPhrase = QString::fromLatin1(parts.mid(2).join(' '));
            m_state = State::Headers;
            break;
        }
        case State::Headers: {
            const int eol = m_buffer.indexOf(crlf);
            if (eol < 0) {
                return;
            }
            const QByteArray line = m_buffer.left(eol);
            m_buffer.remove(0, eol + crlfSize);

            if (line.isEmpty()) {
                // End of headers: decide how the body ends
                if (header("transfer-encoding").toLower().contains("chunked")) {
                    m_bodyMode = BodyMode::Chunked;
                    m_state = State::ChunkSize;
                    break;
                }
                const QByteArray contentLength = header("content-length");
                if (!contentLength.isEmpty()) {
                    bool ok = false;
                    const qint64 length = contentLength.toLongLong(&ok);
                    if (!ok || length < 0) {
                        fail(QStringLiteral("invalid Content-Length"));
                        return;
                    }
                    m_bodyMode = BodyMode::ContentLength;
                    m_expectedBody = length;
                    m_state = length == 0 ? State::Complete : State::Body;
                    break;
                }
                if (m_statusCode == 204 || m_statusCode == 304 || m_statusCode == 100) {
                    m_bodyMode = BodyMode::None;
                    m_state = State::Complete;
                    break;
                }
                // Neither length nor chunked: read until the connection closes
                m_bodyMode = BodyMode::UntilEof;
                m_state = State::Body;
                break;
            }

            const int colon = line.indexOf(':');
            if (colon <= 0) {
                fail(QStringLiteral("malformed header line"));
                return;
            }
            m_headers.append({line.left(colon).trimmed().toLower(), line.mid(colon + 1).trimmed()});
            break;
        }
        case State::Body: {
            if (m_bodyMode == BodyMode::ContentLength) {
                if (m_buffer.size() < m_expectedBody) {
                    return; // wait for more data
                }
                m_body.append(m_buffer.left(m_expectedBody));
                m_buffer.remove(0, m_expectedBody);
                m_state = State::Complete;
            } else {
                // UntilEof: buffer what we have; finishInput() decides the end
                m_body.append(m_buffer);
                m_buffer.clear();
                return;
            }
            break;
        }
        case State::ChunkSize: {
            const int eol = m_buffer.indexOf(crlf);
            if (eol < 0) {
                return;
            }
            QByteArray line = m_buffer.left(eol);
            m_buffer.remove(0, eol + crlfSize);
            const int semicolon = line.indexOf(';');
            if (semicolon >= 0) {
                line = line.left(semicolon); // drop chunk extensions
            }
            bool ok = false;
            const qint64 size = line.trimmed().toLongLong(&ok, 16);
            if (!ok || size < 0) {
                fail(QStringLiteral("invalid chunk size"));
                return;
            }
            if (size == 0) {
                m_state = State::Trailers;
                break;
            }
            m_chunkRemaining = size;
            m_state = State::ChunkData;
            break;
        }
        case State::ChunkData: {
            if (m_buffer.size() < m_chunkRemaining) {
                m_body.append(m_buffer);
                m_chunkRemaining -= m_buffer.size();
                m_buffer.clear();
                return;
            }
            m_body.append(m_buffer.left(m_chunkRemaining));
            m_buffer.remove(0, m_chunkRemaining);
            m_chunkRemaining = 0;
            m_state = State::ChunkTerminator;
            break;
        }
        case State::ChunkTerminator: {
            if (m_buffer.size() < crlfSize) {
                return;
            }
            if (!m_buffer.startsWith(crlf)) {
                fail(QStringLiteral("missing chunk terminator"));
                return;
            }
            m_buffer.remove(0, crlfSize);
            m_state = State::ChunkSize;
            break;
        }
        case State::Trailers: {
            const int eol = m_buffer.indexOf(crlf);
            if (eol < 0) {
                return;
            }
            const QByteArray line = m_buffer.left(eol);
            m_buffer.remove(0, eol + crlfSize);
            if (line.isEmpty()) {
                m_state = State::Complete;
            }
            break;
        }
        case State::Complete:
        case State::Failed:
            return;
        }
    }
}

} // namespace Kontainer
