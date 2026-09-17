/*
    SPDX-FileCopyrightText: 2026 kontainer developers
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
 * 剥离 ANSI 转义序列（CSI / OSC / 两字符转义）。
 *
 * 只做"去掉"，不做任何渲染：§3.1 明确日志是纯显示，不做主题定制。
 * 解析必须容忍**序列跨包**：这里按字节扫描，遇到不完整的序列就把余下的留在
 * `m_pendingLine` 之后的下一次调用里继续（见 appendText 的处理）。
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

        // ESC 开头的序列：先看第二个字节属于哪一类
        if (i + 1 >= input.size()) {
            *incompleteTail = input.mid(i); // 序列被切断了，留给下一次
            return out;
        }
        const char kind = input.at(i + 1);
        if (kind == '[') {
            // CSI：参数直到 0x40–0x7e 的终止字节
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
            // OSC：直到 BEL 或 ST(ESC \)
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
        // 其余两字符转义（ESC c、ESC ( B …）：保守起见最多吃两个字符
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
    // 上一次调用留下的、未结束的转义序列：接到这次数据前面一起解析
    QByteArray combined = m_ansiPending + data;
    m_ansiPending.clear();

    QByteArray incomplete;
    const QByteArray clean = stripAnsi(combined, &incomplete);
    if (!incomplete.isEmpty()) {
        // 序列被切断：留到下一次（绝不能当成正文输出）
        m_ansiPending = incomplete;
    }

    for (const char ch : clean) {
        if (m_crPending) {
            m_crPending = false;
            if (ch == '\n') {
                closePendingLine(stream, out); // CRLF：就是行尾
                continue;
            }
            // 单独的 \r：回车覆盖——先把当前内容作为**临时行**发出去（进度条能实时看到），
            // 再清空缓冲，后面的字节重写这一行；控制台按"临时行"语义替换最后一行
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
    line.complete = false; // 临时：控制台应当替换最后一行，而不是新增一行
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

        // 合法性：流号只能是 1/2（0 = stdin，实测不会出现；其余视为畸形）
        if (streamByte != 1 && streamByte != 2) {
            m_discardedBytes += m_frameBuffer.size();
            m_frameBuffer.clear();
            break;
        }
        if (payloadSize > quint32(kMaxFrameBytes)) {
            // 一个坏长度不能让我们分配 4 GB：丢掉这个帧头并放弃整个缓冲
            m_discardedBytes += m_frameBuffer.size();
            m_frameBuffer.clear();
            break;
        }
        if (quint32(m_frameBuffer.size() - kHeaderBytes) < payloadSize) {
            break; // 半帧：等后续字节
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
        // 流在 `\r` 之后结束：这一行已被覆盖成空（进度条常见形态）
        m_crPending = false;
        m_pendingLine.clear();
        m_hasPending = true;
    }
    if (!m_ansiPending.isEmpty()) {
        // 未结束的转义序列在流结束时没有意义
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
    // 半帧与未完成的转义序列在流结束时没有意义：计入丢弃量，便于自检
    m_discardedBytes += m_frameBuffer.size();
    m_frameBuffer.clear();
    return lines;
}

} // namespace Kontainer
