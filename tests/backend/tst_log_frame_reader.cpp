/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/log_frame_reader.h"

#include <QtTest>

using namespace Kontainer;

/*!
 * Docker 日志字节流的解复用（ARCH_V5_V8 §3.1.1）。
 *
 * 这些用例对应的是"真实日志里一定会遇到、但只靠肉眼很难发现"的情形：
 * 半帧跨包、一个包里多个帧、TTY 的原始流、ANSI 颜色、`\r` 进度条、
 * 以及畸形帧头（一个坏长度不能让进程去分配几个 GB）。
 */
class LogFrameReaderTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void stdcopyFramesAreDemultiplexed();
    void partialFramesAcrossPacketsAreBuffered();
    void multipleFramesInOnePacket();
    void ttyStreamsAreRaw();
    void ansiSequencesAreStrippedAcrossPackets();
    void carriageReturnOverwritesTheCurrentLine();
    void carriageReturnFollowedByNewlineIsJustALineEnd();
    void malformedFramesAreDiscardedSafely();
    void flushEmitsTheTrailingPartialLine();
};

namespace
{
/*! 构造一帧（stream: 1=stdout, 2=stderr）。 */
QByteArray frame(char stream, const QByteArray &payload)
{
    QByteArray out;
    out.append(stream);
    out.append(3, '\0');
    const quint32 size = quint32(payload.size());
    out.append(char((size >> 24) & 0xff));
    out.append(char((size >> 16) & 0xff));
    out.append(char((size >> 8) & 0xff));
    out.append(char(size & 0xff));
    out.append(payload);
    return out;
}

QStringList texts(const QList<LogLine> &lines)
{
    QStringList result;
    for (const LogLine &line : lines) {
        result.append(line.text);
    }
    return result;
}
} // namespace

void LogFrameReaderTest::stdcopyFramesAreDemultiplexed()
{
    LogFrameReader reader;
    const QList<LogLine> lines = reader.feed(frame(1, "hello\n") + frame(2, "warning\n"));
    QCOMPARE(lines.size(), 2);
    QCOMPARE(lines.at(0).text, QStringLiteral("hello"));
    QCOMPARE(lines.at(0).stream, LogLine::Stream::Stdout);
    QCOMPARE(lines.at(1).text, QStringLiteral("warning"));
    QCOMPARE(lines.at(1).stream, LogLine::Stream::Stderr);
    QVERIFY(lines.at(0).complete);
}

void LogFrameReaderTest::partialFramesAcrossPacketsAreBuffered()
{
    // 半帧必须留在缓冲里：真实 socket 的切分点完全随机
    const QByteArray whole = frame(1, "line one\nline two\n");
    LogFrameReader reader;
    QList<LogLine> lines;
    for (int i = 0; i < whole.size(); ++i) {
        lines += reader.feed(whole.mid(i, 1)); // 逐字节喂
    }
    QCOMPARE(texts(lines), QStringList({QStringLiteral("line one"), QStringLiteral("line two")}));
    QCOMPARE(reader.discardedBytes(), 0);
}

void LogFrameReaderTest::multipleFramesInOnePacket()
{
    LogFrameReader reader;
    const QList<LogLine> lines = reader.feed(frame(1, "a\n") + frame(2, "b\n") + frame(1, "c\n"));
    QCOMPARE(texts(lines), QStringList({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}));
    QCOMPARE(lines.at(1).stream, LogLine::Stream::Stderr);
}

void LogFrameReaderTest::ttyStreamsAreRaw()
{
    // TTY 容器没有帧头：一律当原始字节，否则会把正文当成帧头解析掉
    LogFrameReader reader(true);
    QVERIFY(reader.isTty());
    QList<LogLine> lines = reader.feed("first\nthe ");
    QCOMPARE(texts(lines), QStringList {QStringLiteral("first")});
    lines = reader.feed("second\n");
    QCOMPARE(texts(lines), QStringList {QStringLiteral("the second")});
    QCOMPARE(lines.at(0).stream, LogLine::Stream::Stdout);
    QCOMPARE(reader.discardedBytes(), 0);
}

void LogFrameReaderTest::ansiSequencesAreStrippedAcrossPackets()
{
    LogFrameReader reader;
    // 颜色：\x1b[31m ... \x1b[0m
    QList<LogLine> lines = reader.feed(frame(1, "\x1b[31mred\x1b[0m text\n"));
    QCOMPARE(texts(lines), QStringList {QStringLiteral("red text")});

    // 序列被切在两个包之间：不能把 ESC[31 当成正文漏出去
    const QByteArray colored = frame(1, "still \x1b[1;32mgreen\x1b[0m\n");
    const int splitAt = colored.indexOf("\x1b") + 2;
    LogFrameReader split;
    lines = split.feed(colored.left(splitAt));
    lines += split.feed(colored.mid(splitAt));
    QCOMPARE(texts(lines), QStringList {QStringLiteral("still green")});

    // OSC（终端标题等）也要整段丢掉
    LogFrameReader osc;
    lines = osc.feed(frame(1, "\x1b]0;title" "\x07" "after\n"));
    QCOMPARE(texts(lines), QStringList {QStringLiteral("after")});
}

void LogFrameReaderTest::carriageReturnOverwritesTheCurrentLine()
{
    // 进度条：反复 \r 只保留最后一次覆盖后的内容
    LogFrameReader reader;
    const QList<LogLine> lines = reader.feed(frame(1, "10%\r50%\r100% done\nnext\n"));
    QCOMPARE(texts(lines), QStringList({QStringLiteral("100% done"), QStringLiteral("next")}));
}

void LogFrameReaderTest::carriageReturnFollowedByNewlineIsJustALineEnd()
{
    // CRLF 是行尾，不是"覆盖"：内容必须保留
    LogFrameReader reader;
    const QList<LogLine> lines = reader.feed(frame(1, "windows line\r\nsecond\r\n"));
    QCOMPARE(texts(lines), QStringList({QStringLiteral("windows line"), QStringLiteral("second")}));
}

void LogFrameReaderTest::malformedFramesAreDiscardedSafely()
{
    // 流号非法：丢掉这一帧头与缓冲，不做任何分配
    LogFrameReader badStream;
    QList<LogLine> lines = badStream.feed(QByteArray("\x07\x00\x00\x00\x00\x00\x00\x04junk", 12));
    QVERIFY(lines.isEmpty());
    QVERIFY(badStream.discardedBytes() > 0);
    // 之后仍然能正常工作（清空缓冲后重新同步）
    lines = badStream.feed(frame(1, "recovered\n"));
    QCOMPARE(texts(lines), QStringList {QStringLiteral("recovered")});

    // 声称 4 GiB 的帧：绝不能照单分配
    LogFrameReader huge;
    QByteArray hugeHeader;
    hugeHeader.append(char(1));
    hugeHeader.append(3, '\0');
    hugeHeader.append(char(0xff));
    hugeHeader.append(char(0xff));
    hugeHeader.append(char(0xff));
    hugeHeader.append(char(0xff));
    lines = huge.feed(hugeHeader);
    QVERIFY(lines.isEmpty());
    QVERIFY(huge.discardedBytes() >= hugeHeader.size());
}

void LogFrameReaderTest::flushEmitsTheTrailingPartialLine()
{
    LogFrameReader reader;
    QList<LogLine> lines = reader.feed(frame(1, "complete\nhalf"));
    QCOMPARE(texts(lines), QStringList {QStringLiteral("complete")});

    const QList<LogLine> tail = reader.flush();
    QCOMPARE(tail.size(), 1);
    QCOMPARE(tail.at(0).text, QStringLiteral("half"));
    QVERIFY2(!tail.at(0).complete, "the trailing line was never terminated by a newline");

    // 结束后状态清空：再 flush 不该重复输出
    QVERIFY(reader.flush().isEmpty());

    // 流在 \r 之后结束：那一行已被覆盖成空（进度条常见形态）
    LogFrameReader overwritten;
    overwritten.feed(frame(1, "progress 10%\r"));
    const QList<LogLine> empty = overwritten.flush();
    QCOMPARE(empty.size(), 1);
    QVERIFY(empty.at(0).text.isEmpty());
}

QTEST_GUILESS_MAIN(LogFrameReaderTest)

#include "tst_log_frame_reader.moc"
