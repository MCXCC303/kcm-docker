/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/http/json_line_reader.h"

#include <QJsonObject>
#include <QtTest>

using namespace Kontainer;

/*!
 * 镜像拉取流的行解析（ARCH_V4 §2.2.1 / §5.1）：
 * TCP 分片与 JSON 行边界无关，这里覆盖「一行跨多 chunk」「一个 chunk 多行」
 * 「半行缓存」「畸形行不中断流」「超长行防御」四种情况。
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
    // 一个 JSON 对象被切成三段，中间还夹着下一个对象的开头
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
    // 两行非法：一行不是 JSON，一行是数组（不是对象）
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
    // 超过上限且没有换行的垃圾数据：整行丢弃，但后续正常行仍要解析出来
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
