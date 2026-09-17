/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/http/http_response_parser.h"

#include <QtTest>

using namespace Kontainer;

class HttpResponseParserTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parsesContentLengthResponse();
    void parsesChunkedResponse();
    void parsesChunkedResponseSplitAcrossFeeds();
    void parsesChunkedResponseWithExtensionsAndTrailers();
    void parsesBodyUntilConnectionClose();
    void headerLookupIsCaseInsensitive();
    void incompleteResponseWaitsForMoreData();
    void rejectsInvalidStatusLine();
    void rejectsInvalidContentLength();
    void rejectsMissingChunkTerminator();
    void finishInputFailsOnTruncatedResponse();
    void takeBodyReturnsOnlyNewBytes();
    void takeBodyKeepsFullBodySemantics();
};

void HttpResponseParserTest::parsesContentLengthResponse()
{
    HttpResponseParser parser;
    parser.feed("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\nOK");
    QVERIFY(parser.isComplete());
    QCOMPARE(parser.statusCode(), 200);
    QCOMPARE(parser.reasonPhrase(), QStringLiteral("OK"));
    QCOMPARE(parser.body(), QByteArray("OK"));
    QCOMPARE(parser.header("content-type"), QByteArray("application/json"));
}

void HttpResponseParserTest::parsesChunkedResponse()
{
    // /info 与 /containers/json 使用 chunked 传输（ARCH_V1 §7 实测行为）
    HttpResponseParser parser;
    parser.feed("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                "5\r\n{\"a\":\r\n"
                "3\r\n1}\n\r\n"
                "0\r\n\r\n");
    QVERIFY(parser.isComplete());
    QCOMPARE(parser.body(), QByteArray("{\"a\":1}\n"));
}

void HttpResponseParserTest::parsesChunkedResponseSplitAcrossFeeds()
{
    HttpResponseParser parser;
    const QByteArray payload = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n4\r\nWiki\r\n5\r\npedia\r\n0\r\n\r\n";

    // 逐字节喂入：解析器必须在任意切分点都能正确推进
    for (int i = 0; i < payload.size(); ++i) {
        parser.feed(payload.mid(i, 1));
    }
    QVERIFY(parser.isComplete());
    QCOMPARE(parser.body(), QByteArray("Wikipedia"));
}

void HttpResponseParserTest::parsesChunkedResponseWithExtensionsAndTrailers()
{
    HttpResponseParser parser;
    parser.feed("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                "5;ext=1\r\nhello\r\n"
                "0\r\nX-Trailer: value\r\n\r\n");
    QVERIFY(parser.isComplete());
    QCOMPARE(parser.body(), QByteArray("hello"));
}

void HttpResponseParserTest::parsesBodyUntilConnectionClose()
{
    HttpResponseParser parser;
    parser.feed("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n");
    QVERIFY(!parser.isComplete());

    parser.feed("par");
    parser.feed("tial");
    QVERIFY(!parser.isComplete());

    parser.finishInput();
    QVERIFY(parser.isComplete());
    QCOMPARE(parser.body(), QByteArray("partial"));
}

void HttpResponseParserTest::headerLookupIsCaseInsensitive()
{
    HttpResponseParser parser;
    parser.feed("HTTP/1.1 404 Not Found\r\nX-Docker-Experimental: false\r\nContent-Length: 0\r\n\r\n");
    QVERIFY(parser.isComplete());
    QCOMPARE(parser.statusCode(), 404);
    QCOMPARE(parser.header("x-docker-experimental"), QByteArray("false"));
    QCOMPARE(parser.header("X-DOCKER-EXPERIMENTAL"), QByteArray("false"));
}

void HttpResponseParserTest::incompleteResponseWaitsForMoreData()
{
    HttpResponseParser parser;
    parser.feed("HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\n12345");
    QVERIFY(!parser.isComplete());
    QVERIFY(!parser.isFailed());

    parser.feed("67890");
    QVERIFY(parser.isComplete());
    QCOMPARE(parser.body(), QByteArray("1234567890"));
}

void HttpResponseParserTest::rejectsInvalidStatusLine()
{
    HttpResponseParser parser;
    parser.feed("NOT-HTTP garbage\r\n\r\n");
    QVERIFY(parser.isFailed());
    QVERIFY(!parser.errorString().isEmpty());
}

void HttpResponseParserTest::rejectsInvalidContentLength()
{
    HttpResponseParser parser;
    parser.feed("HTTP/1.1 200 OK\r\nContent-Length: abc\r\n\r\n");
    QVERIFY(parser.isFailed());
}

void HttpResponseParserTest::rejectsMissingChunkTerminator()
{
    HttpResponseParser parser;
    parser.feed("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhelloXX");
    QVERIFY(parser.isFailed());
}

void HttpResponseParserTest::finishInputFailsOnTruncatedResponse()
{
    HttpResponseParser parser;
    parser.feed("HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\n12345");
    parser.finishInput();
    QVERIFY(parser.isFailed());
}

/*!
 * 流式响应（镜像拉取）靠 takeBody() 增量取值：
 * 只返回「上次取走之后新增」的部分，且不能丢字节。
 */
void HttpResponseParserTest::takeBodyReturnsOnlyNewBytes()
{
    HttpResponseParser parser;

    // 头部 + 第一个 chunk（"{\"a\":1}\n" 共 8 字节）
    parser.feed("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n8\r\n{\"a\":1}\n\r\n");
    QCOMPARE(parser.takeBody(), QByteArray("{\"a\":1}\n"));
    QVERIFY(parser.takeBody().isEmpty());

    // 第二个 chunk（"{\"b\":2}\n"），同样是增量
    parser.feed("8\r\n{\"b\":2}\n\r\n");
    QCOMPARE(parser.takeBody(), QByteArray("{\"b\":2}\n"));

    // 结束 chunk
    parser.feed("0\r\n\r\n");
    QVERIFY(parser.isComplete());
    QVERIFY(parser.takeBody().isEmpty());

    // 增量之和 == 全量
    QCOMPARE(parser.body(), QByteArray("{\"a\":1}\n{\"b\":2}\n"));
}

/*!
 * `body()` 的全量语义不受 takeBody() 影响——非流式调用方（所有只读请求）
 * 拿到的仍然是完整响应体。
 */
void HttpResponseParserTest::takeBodyKeepsFullBodySemantics()
{
    HttpResponseParser parser;
    parser.feed("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello");
    QCOMPARE(parser.takeBody(), QByteArray("hello"));
    QCOMPARE(parser.body(), QByteArray("hello"));

    parser.reset();
    QVERIFY(parser.takeBody().isEmpty());
    parser.feed("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nhi");
    QCOMPARE(parser.takeBody(), QByteArray("hi"));
}

QTEST_GUILESS_MAIN(HttpResponseParserTest)

#include "tst_http_response_parser.moc"
