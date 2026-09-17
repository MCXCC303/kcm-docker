/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>

namespace Kontainer
{

/*!
 * 字节流 → 完整 JSON 行（ARCH_V4 §2.2.1）。
 *
 * Docker 的 `POST /images/create` 返回的是一串「一行一个 JSON 对象」的流，
 * TCP 分片与 JSON 行边界毫无关系：一行可能跨多个 chunk，一个 chunk 也可能含多行。
 * 这个类只做拼行与解析，不做 I/O、不判断业务语义，因此可以脱离 socket 单测。
 *
 * 非法行不会被抛成错误：拉取过程中出现一行看不懂的内容不应该让整个拉取失败，
 * 调用方可以用 malformedLines() 观察并记录（只记数量，不记内容）。
 */
class JsonLineReader
{
public:
    /*! 行长度上限：超过即丢弃整行（防御性上限，避免畸形流把内存吃掉）。 */
    static constexpr int kMaxLineBytes = 1024 * 1024;

    /*! 喂入一段字节，返回其中已经完整解析出来的 JSON 对象。 */
    QList<QJsonObject> feed(const QByteArray &chunk);
    /*! 流结束：把最后一行（可能没有换行符）也解析出来。 */
    QList<QJsonObject> finish();

    /*! 解析失败的行数（只用于日志计数）。 */
    int malformedLines() const
    {
        return m_malformedLines;
    }
    /*! 因超长被丢弃的行数。 */
    int droppedLines() const
    {
        return m_droppedLines;
    }
    /*! 当前还留在缓冲区里的半行字节数。 */
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
