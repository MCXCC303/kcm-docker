/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/log_frame_reader.h"

#include <QtTest>

using namespace Kontainer;

/*!
 * Demultiplexing the Docker log byte stream (ARCH_V5_V8 §3.1.1).
 *
 * These cases are what real logs always contain but the eye rarely catches:
 * half frames across packets, several frames per packet, raw TTY streams, ANSI
 * color, `\r` progress bars, and malformed headers (a bad length must not make
 * the process allocate gigabytes).
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
/*! Build one frame (stream: 1=stdout, 2=stderr). */
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
    // Half frames must stay buffered: real socket split points are fully random
    const QByteArray whole = frame(1, "line one\nline two\n");
    LogFrameReader reader;
    QList<LogLine> lines;
    for (int i = 0; i < whole.size(); ++i) {
        lines += reader.feed(whole.mid(i, 1)); // feed one byte at a time
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
    // TTY containers have no frame headers: treat everything as raw bytes, else payload is parsed as a header
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
    // Color: \x1b[31m ... \x1b[0m
    QList<LogLine> lines = reader.feed(frame(1, "\x1b[31mred\x1b[0m text\n"));
    QCOMPARE(texts(lines), QStringList {QStringLiteral("red text")});

    // Sequence split across two packets: ESC[31 must not leak out as text
    const QByteArray colored = frame(1, "still \x1b[1;32mgreen\x1b[0m\n");
    const int splitAt = colored.indexOf("\x1b") + 2;
    LogFrameReader split;
    lines = split.feed(colored.left(splitAt));
    lines += split.feed(colored.mid(splitAt));
    QCOMPARE(texts(lines), QStringList {QStringLiteral("still green")});

    // OSC (terminal titles etc.) must be dropped whole as well
    LogFrameReader osc;
    lines = osc.feed(frame(1, "\x1b]0;title" "\x07" "after\n"));
    QCOMPARE(texts(lines), QStringList {QStringLiteral("after")});
}

void LogFrameReaderTest::carriageReturnOverwritesTheCurrentLine()
{
    // Progress bar: each overwrite emits a **provisional line** (complete == false) that the
    // console replaces the last line with, so progress stays live without flooding it
    LogFrameReader reader;
    const QList<LogLine> lines = reader.feed(frame(1, "10%\r50%\r100% done\nnext\n"));
    QCOMPARE(texts(lines), QStringList({QStringLiteral("10%"), QStringLiteral("50%"), QStringLiteral("100% done"), QStringLiteral("next")}));
    QVERIFY2(!lines.at(0).complete, "an overwritten line is provisional");
    QVERIFY2(!lines.at(1).complete, "an overwritten line is provisional");
    QVERIFY2(lines.at(2).complete, "the line terminated by a newline is final");
    QVERIFY(lines.at(3).complete);
}

void LogFrameReaderTest::carriageReturnFollowedByNewlineIsJustALineEnd()
{
    // CRLF is a line end, not an overwrite: the content must be kept
    LogFrameReader reader;
    const QList<LogLine> lines = reader.feed(frame(1, "windows line\r\nsecond\r\n"));
    QCOMPARE(texts(lines), QStringList({QStringLiteral("windows line"), QStringLiteral("second")}));
}

void LogFrameReaderTest::malformedFramesAreDiscardedSafely()
{
    // Invalid stream number: drop this frame header and the buffer, allocate nothing
    LogFrameReader badStream;
    QList<LogLine> lines = badStream.feed(QByteArray("\x07\x00\x00\x00\x00\x00\x00\x04junk", 12));
    QVERIFY(lines.isEmpty());
    QVERIFY(badStream.discardedBytes() > 0);
    // Still works afterwards (resyncs after clearing the buffer)
    lines = badStream.feed(frame(1, "recovered\n"));
    QCOMPARE(texts(lines), QStringList {QStringLiteral("recovered")});

    // A frame claiming 4 GiB: must never be allocated as claimed
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

    // State is cleared at the end: a second flush must not repeat output
    QVERIFY(reader.flush().isEmpty());

    // Stream ends right after \r: that line was already overwritten to empty (typical progress bar)
    LogFrameReader overwritten;
    overwritten.feed(frame(1, "progress 10%\r"));
    const QList<LogLine> empty = overwritten.flush();
    QCOMPARE(empty.size(), 1);
    QVERIFY(empty.at(0).text.isEmpty());
}

QTEST_GUILESS_MAIN(LogFrameReaderTest)

#include "tst_log_frame_reader.moc"
