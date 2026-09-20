/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
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
    // /info and /containers/json use chunked transfer (measured behavior, ARCH_V1 §7)
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

    // Feed one byte at a time: the parser must advance at any split point
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
 * Streaming responses (image pull) take increments via takeBody():
 * it returns only the part added since the previous call, and must not drop bytes.
 */
void HttpResponseParserTest::takeBodyReturnsOnlyNewBytes()
{
    HttpResponseParser parser;

    // Headers + first chunk ("{\"a\":1}\n", 8 bytes)
    parser.feed("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n8\r\n{\"a\":1}\n\r\n");
    QCOMPARE(parser.takeBody(), QByteArray("{\"a\":1}\n"));
    QVERIFY(parser.takeBody().isEmpty());

    // Second chunk ("{\"b\":2}\n"), also incremental
    parser.feed("8\r\n{\"b\":2}\n\r\n");
    QCOMPARE(parser.takeBody(), QByteArray("{\"b\":2}\n"));

    // Terminating chunk
    parser.feed("0\r\n\r\n");
    QVERIFY(parser.isComplete());
    QVERIFY(parser.takeBody().isEmpty());

    // Sum of increments == full body
    QCOMPARE(parser.body(), QByteArray("{\"a\":1}\n{\"b\":2}\n"));
}

/*!
 * `body()` keeps its full-body semantics regardless of takeBody() - non-streaming callers
 * (all read-only requests) still get the complete response body.
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
