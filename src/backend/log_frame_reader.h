/*
    SPDX-FileCopyrightText: 2026 kontainer developers
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
 * 一条日志片段（ARCH_V5_V8 §3.1.1）。
 *
 * `text` **不含行尾换行符**：是否成行由 `complete` 说明，控制台据此决定追加还是
 * 替换最后一行（`\r` 覆盖的进度行就是这样处理的：每次覆盖发一条 `complete == false`
 * 的临时行，控制台替换掉上一行，因此进度条不会把控制台刷爆，也不会只有等到换行才可见）。
 */
struct LogLine {
    enum class Stream {
        Stdout,
        Stderr,
    };

    Stream stream = Stream::Stdout;
    QString text;
    /*! 是否由 `\n` 结束（false = 流结束时残留的半行）。 */
    bool complete = false;

    bool operator==(const LogLine &other) const
    {
        return stream == other.stream && text == other.text && complete == other.complete;
    }
};

/*!
 * Docker 日志字节流 → 行（ARCH_V5_V8 §3.1.1）。
 *
 * 这是**纯计算**：不碰 socket、不碰界面，因此"半帧、跨包、畸形帧、ANSI、回车覆盖"
 * 这些最容易出错的细节可以单独钉死。
 *
 * 两种流形态（实测结论见 ARCH_V5_V8 附录 A.4）：
 *
 *  - **非 TTY**：每帧 8 字节头（1 = stdout / 2 = stderr，3 字节保留，4 字节大端长度）+ 载荷；
 *    帧可以跨包（半帧要留在缓冲里等后续字节）
 *  - **TTY**：没有帧，就是原始字节流（因此必须按 `Config.Tty` 分支，不能一律当帧解析）
 *
 * 另外两件真实日志里一定会遇到的事：
 *
 *  - **ANSI 转义序列**（颜色）：默认剥离——控制台不做主题定制（§3.1 范围）
 *  - **`\r` 回车覆盖**（进度条）：视为"重写当前行"，因此只保留最后一次覆盖后的内容，
 *    避免进度条把控制台刷爆
 */
class LogFrameReader
{
public:
    /*! `tty` 为真时按原始字节流处理（没有 8 字节帧头）。 */
    explicit LogFrameReader(bool tty = false);

    /*! 追加一段来自 socket 的字节；返回其中已经成行的部分。 */
    QList<LogLine> feed(const QByteArray &data);

    /*!
     * 流结束：交出残留的半行（如果有），并清空状态。
     *
     * `complete` 为 false——它确实没有以换行结束（容器可能正在输出提示符）。
     */
    QList<LogLine> flush();

    /*! 当前是否处于 TTY（原始）模式。 */
    bool isTty() const
    {
        return m_tty;
    }

    /*! 被丢弃的畸形数据字节数（非法帧头 / 超长帧）：用于自检与断言。 */
    qint64 discardedBytes() const
    {
        return m_discardedBytes;
    }

    /*! 单个帧载荷上限：超过它的一定不是日志行，直接丢弃（防止一个坏长度吃掉内存）。 */
    static constexpr int kMaxFrameBytes = 1024 * 1024;

private:
    /*! 把一段 UTF-8 文本按行/回车切分并发出（ANSI 已剥离）。 */
    void appendText(LogLine::Stream stream, const QByteArray &data, QList<LogLine> *out);
    /*! 把当前待定行作为一条完整行发出。 */
    void closePendingLine(LogLine::Stream stream, QList<LogLine> *out);
    /*! 把当前待定行作为**临时行**发出（`\r` 覆盖与流结束用）。 */
    void emitProvisionalLine(QList<LogLine> *out);

    bool m_tty = false;
    /*! 非 TTY 模式下未凑齐一帧的字节。 */
    QByteArray m_frameBuffer;
    /*! 当前待定行（尚未遇到 `\n`）。 */
    QByteArray m_pendingLine;
    LogLine::Stream m_pendingStream = LogLine::Stream::Stdout;
    bool m_hasPending = false;
    /*! 上一次输出以 `\n` 结束（用于把不带换行的片段正确接到新行）。 */
    bool m_pendingStartsNewLine = true;
    /*! 刚读到 `\r`，还没确定它是 CRLF 的行尾还是"覆盖当前行"。 */
    bool m_crPending = false;
    /*! 被切断的 ANSI 转义序列：留到下一次 feed 继续解析。 */
    QByteArray m_ansiPending;
    qint64 m_discardedBytes = 0;
};

} // namespace Kontainer

Q_DECLARE_METATYPE(Kontainer::LogLine)
