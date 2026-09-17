/*
    SPDX-FileCopyrightText: 2026 kontainer developers
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
 * 增量式 HTTP/1.1 响应解析器（ARCH_V1 §10：薄封装，不引入外部 HTTP 库）。
 *
 * 纯计算、无 I/O、无线程：喂入字节流即可，便于单元测试覆盖
 * Content-Length / chunked / 连接关闭 三种结束方式与各种畸形输入。
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

    /*! 追加收到的字节并尽力推进解析。 */
    void feed(const QByteArray &data);
    /*! 对端关闭连接：判定响应是否已经完整（用于 Connection: close 的响应）。 */
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
    /*! 大小写不敏感的头部读取；不存在时返回空。 */
    QByteArray header(const QByteArray &name) const;
    /*! 已完成解码的响应体（chunked 已合并）。 */
    QByteArray body() const
    {
        return m_body;
    }
    /*!
     * 取走尚未被消费的响应体增量（ARCH_V4 §2.2.1）。
     *
     * `body()` 的全量语义不变；本方法只把「已取走」的位置向前推进，
     * 供流式响应（镜像拉取进度）边收边解析。非流式调用方完全不需要它。
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
    /*! 已被 takeBody() 取走的字节数（body() 仍然返回全量）。 */
    qsizetype m_bodyConsumed = 0;
    QList<QPair<QByteArray, QByteArray>> m_headers;

    qint64 m_expectedBody = 0;
    qint64 m_chunkRemaining = 0;
    int m_statusCode = 0;
    QString m_reasonPhrase;
    QString m_errorString;
};

} // namespace Kontainer
