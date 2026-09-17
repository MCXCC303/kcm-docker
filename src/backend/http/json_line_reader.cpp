/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/http/json_line_reader.h"

#include <QJsonDocument>
#include <QJsonParseError>

namespace Kontainer
{

void JsonLineReader::reset()
{
    m_buffer.clear();
    m_malformedLines = 0;
    m_droppedLines = 0;
    m_droppingOversizedLine = false;
}

void JsonLineReader::consumeLine(const QByteArray &line, QList<QJsonObject> &out)
{
    QByteArray candidate = line;
    if (candidate.endsWith('\r')) {
        candidate.chop(1);
    }
    if (candidate.trimmed().isEmpty()) {
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(candidate, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        // 单行看不懂不影响整条流：只计数（不记内容，流里可能含镜像名等用户数据）
        ++m_malformedLines;
        return;
    }
    out.append(document.object());
}

QList<QJsonObject> JsonLineReader::takeCompleteLines()
{
    QList<QJsonObject> out;

    while (true) {
        if (m_droppingOversizedLine) {
            const int newline = m_buffer.indexOf('\n');
            if (newline < 0) {
                m_buffer.clear();
                return out;
            }
            m_buffer.remove(0, newline + 1);
            m_droppingOversizedLine = false;
            continue;
        }

        const int newline = m_buffer.indexOf('\n');
        if (newline < 0) {
            // 没有换行：要么是半行，要么是一行超长数据（防御性丢弃）
            if (m_buffer.size() > kMaxLineBytes) {
                m_buffer.clear();
                m_droppingOversizedLine = true;
                ++m_droppedLines;
            }
            return out;
        }

        const QByteArray line = m_buffer.left(newline);
        m_buffer.remove(0, newline + 1);
        if (line.size() > kMaxLineBytes) {
            ++m_droppedLines;
            continue;
        }
        consumeLine(line, out);
    }
}

QList<QJsonObject> JsonLineReader::feed(const QByteArray &chunk)
{
    if (!chunk.isEmpty()) {
        m_buffer.append(chunk);
    }
    return takeCompleteLines();
}

QList<QJsonObject> JsonLineReader::finish()
{
    QList<QJsonObject> out;
    if (!m_droppingOversizedLine && !m_buffer.isEmpty()) {
        const QByteArray line = m_buffer;
        m_buffer.clear();
        if (line.size() > kMaxLineBytes) {
            ++m_droppedLines;
        } else {
            consumeLine(line, out);
        }
    }
    m_buffer.clear();
    m_droppingOversizedLine = false;
    return out;
}

} // namespace Kontainer
