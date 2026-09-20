/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/http/json_line_reader.h"

#include <QJsonObject>
#include <QtTest>

using namespace Kontainer;

/*!
 * Line parsing for the image pull stream (ARCH_V4 §2.2.1 / §5.1):
 * TCP chunking is unrelated to JSON line boundaries. Covers one line across
 * chunks, many lines in one chunk, partial-line buffering, malformed lines not
 * stopping the stream, and oversized-line defense.
 */
class JsonLineReaderTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parsesOneObjectPerLine();
    void joinsLineSplitAcrossChunks();
    void parsesManyLinesInOneChunk();
    void finishParsesTrailingLineWithoutNewline();
    void malformedLinesDoNotStopTheStream();
    void toleratesCarriageReturnsAndBlankLines();
    void oversizedLineIsDroppedAndStreamContinues();
};

void JsonLineReaderTest::parsesOneObjectPerLine()
{
    JsonLineReader reader;
    const QList<QJsonObject> objects = reader.feed("{\"status\":\"Pulling\"}\n{\"status\":\"Done\"}\n");
    QCOMPARE(objects.size(), 2);
    QCOMPARE(objects.at(0).value(QStringLiteral("status")).toString(), QStringLiteral("Pulling"));
    QCOMPARE(objects.at(1).value(QStringLiteral("status")).toString(), QStringLiteral("Done"));
    QCOMPARE(reader.malformedLines(), 0);
    QCOMPARE(reader.pendingBytes(), 0);
}

void JsonLineReaderTest::joinsLineSplitAcrossChunks()
{
    JsonLineReader reader;
    // One JSON object split across three chunks, with the next object's head tagged on
    QVERIFY(reader.feed("{\"progressDetail\":{\"cur").isEmpty());
    QVERIFY(reader.feed("rent\":50,\"total\":100},\"id\":\"ab").isEmpty());

    const QList<QJsonObject> objects = reader.feed("c\"}\n{\"status\":\"Extracting\"}\n");
    QCOMPARE(objects.size(), 2);

    const QJsonObject progress = objects.at(0).value(QStringLiteral("progressDetail")).toObject();
    QCOMPARE(progress.value(QStringLiteral("current")).toInt(), 50);
    QCOMPARE(objects.at(0).value(QStringLiteral("id")).toString(), QStringLiteral("abc"));
    QCOMPARE(reader.malformedLines(), 0);
}

void JsonLineReaderTest::parsesManyLinesInOneChunk()
{
    JsonLineReader reader;
    QByteArray chunk;
    for (int i = 0; i < 25; ++i) {
        chunk += QByteArray("{\"id\":\"") + QByteArray::number(i) + "\"}\n";
    }
    const QList<QJsonObject> objects = reader.feed(chunk);
    QCOMPARE(objects.size(), 25);
    QCOMPARE(objects.last().value(QStringLiteral("id")).toString(), QStringLiteral("24"));
}

void JsonLineReaderTest::finishParsesTrailingLineWithoutNewline()
{
    JsonLineReader reader;
    QVERIFY(reader.feed("{\"status\":\"first\"}\n{\"status\":\"last\"}").size() == 1);
    QCOMPARE(reader.pendingBytes(), int(qstrlen("{\"status\":\"last\"}")));

    const QList<QJsonObject> tail = reader.finish();
    QCOMPARE(tail.size(), 1);
    QCOMPARE(tail.at(0).value(QStringLiteral("status")).toString(), QStringLiteral("last"));
    QCOMPARE(reader.pendingBytes(), 0);
    QVERIFY(reader.finish().isEmpty());
}

void JsonLineReaderTest::malformedLinesDoNotStopTheStream()
{
    JsonLineReader reader;
    const QList<QJsonObject> objects = reader.feed("not json\n{\"ok\":true}\n[1,2,3]\n\n{\"also\":true}\n");
    QCOMPARE(objects.size(), 2);
    QVERIFY(objects.at(0).value(QStringLiteral("ok")).toBool());
    QVERIFY(objects.at(1).value(QStringLiteral("also")).toBool());
    // Two bad lines: one is not JSON, one is an array (not an object)
    QCOMPARE(reader.malformedLines(), 2);
}

void JsonLineReaderTest::toleratesCarriageReturnsAndBlankLines()
{
    JsonLineReader reader;
    const QList<QJsonObject> objects = reader.feed("{\"a\":1}\r\n\r\n  \r\n{\"b\":2}\r\n");
    QCOMPARE(objects.size(), 2);
    QCOMPARE(reader.malformedLines(), 0);
}

void JsonLineReaderTest::oversizedLineIsDroppedAndStreamContinues()
{
    JsonLineReader reader;
    // Oversized garbage with no newline: drop the whole line, but keep parsing later good lines
    const QByteArray garbage(JsonLineReader::kMaxLineBytes + 16, 'x');
    QVERIFY(reader.feed(garbage).isEmpty());

    const QList<QJsonObject> objects = reader.feed("tail-of-garbage\n{\"ok\":true}\n");
    QCOMPARE(objects.size(), 1);
    QVERIFY(objects.at(0).value(QStringLiteral("ok")).toBool());
    QCOMPARE(reader.droppedLines(), 1);
    QCOMPARE(reader.malformedLines(), 0);
}

QTEST_GUILESS_MAIN(JsonLineReaderTest)

#include "tst_json_line_reader.moc"
