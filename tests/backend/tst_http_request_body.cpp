/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/docker_client.h"
#include "backend/docker_endpoint.h"

#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace Kontainer;

/*!
 * Streaming upload request body (ARCH_V5_V8 §5.1).
 *
 * The only new transport capability of phase 8: building an image uploads a tar context, and
 * a body of tens to hundreds of MB cannot go into memory at once, nor may the "response phase"
 * idle timeout be used to bound the upload. So these cases watch the edges: length matches the
 * file, chunked bytes are written verbatim, an early remote close errors out, cancel aborts.
 */
class HttpRequestBodyTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();

    void uploadsChunkedLargeFile();
    void reportsErrorWhenFileIsMissing();
    void reportsErrorWhenServerClosesEarly();
    void cancelStopsTheUpload();

private:
    /*! Minimal HTTP server: hands the received bytes to the test verbatim. */
    QLocalServer m_server;
    QLocalSocket *m_connection = nullptr;
    QByteArray m_received;
    int m_firstResponseDelayMs = 0;
    bool m_closeEarly = false;
    QString m_socketPath;

    void acceptConnection();
    void onReadyRead();
};

void HttpRequestBodyTest::initTestCase()
{
    QVERIFY(m_server.listen(QStringLiteral("kontainer-body-test")));
    m_socketPath = m_server.fullServerName();
    connect(&m_server, &QLocalServer::newConnection, this, &HttpRequestBodyTest::acceptConnection);
}

void HttpRequestBodyTest::init()
{
    m_received.clear();
    m_firstResponseDelayMs = 0;
    m_closeEarly = false;
}

void HttpRequestBodyTest::acceptConnection()
{
    m_connection = m_server.nextPendingConnection();
    QVERIFY(m_connection);
    connect(m_connection, &QLocalSocket::readyRead, this, &HttpRequestBodyTest::onReadyRead);
}

void HttpRequestBodyTest::onReadyRead()
{
    m_received += m_connection->readAll();
    const int headerEnd = m_received.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        return;
    }
    const QByteArray headers = m_received.left(headerEnd);
    const int lengthIndex = headers.indexOf("Content-Length: ");
    const int expected = lengthIndex >= 0 ? headers.mid(lengthIndex + 16).split('\r').value(0).toInt() : 0;
    const QByteArray body = m_received.mid(headerEnd + 4);
    if (body.size() < expected) {
        return; // body not complete yet
    }
    if (m_closeEarly) {
        m_connection->disconnectFromServer();
        return;
    }
    if (m_firstResponseDelayMs > 0) {
        QTest::qWait(m_firstResponseDelayMs);
    }
    m_connection->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}");
    m_connection->flush();
    m_connection->disconnectFromServer();
}

/*! Large file (> one chunk): the uploaded content must match the file exactly. */
void HttpRequestBodyTest::uploadsChunkedLargeFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("big-context.tar"));
    // 1 MiB and a bit: spans several 256 KiB chunks
    QByteArray payload;
    payload.reserve(1024 * 1024 + 7);
    for (int i = 0; i < payload.capacity(); ++i) {
        payload.append(char(i % 251));
    }
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(payload), qint64(payload.size()));
    file.close();

    DockerClient client;
    client.setEndpoint(DockerEndpoint::unixSocket(m_socketPath));

    DockerReply *reply = client.postFile(QStringLiteral("/build"),
                                         QUrlQuery(),
                                         path,
                                         QByteArrayLiteral("application/x-tar"),
                                         5000,
                                         5000);
    QSignalSpy finishedSpy(reply, &DockerReply::finished);
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 20000);
    QCOMPARE(reply->state(), DockerReply::State::Succeeded);

    const int headerEnd = m_received.indexOf("\r\n\r\n");
    QVERIFY(headerEnd > 0);
    const QByteArray headers = m_received.left(headerEnd);
    QVERIFY(headers.contains("Content-Type: application/x-tar"));
    QVERIFY(headers.contains("Content-Length: " + QByteArray::number(payload.size())));
    QCOMPARE(m_received.mid(headerEnd + 4), payload); // byte for byte
    reply->deleteLater();
}

void HttpRequestBodyTest::reportsErrorWhenFileIsMissing()
{
    DockerClient client;
    client.setEndpoint(DockerEndpoint::unixSocket(m_socketPath));

    DockerReply *reply = client.postFile(QStringLiteral("/build"),
                                         QUrlQuery(),
                                         QStringLiteral("/nonexistent/context.tar"),
                                         QByteArrayLiteral("application/x-tar"),
                                         5000,
                                         5000);
    // Poll the state instead of the signal: the failure can happen right after the connection
    // is established, so the signal may already have fired when the caller (and this test) gets
    // the reply - the client therefore defers emitting this kind of failure
    QTRY_COMPARE_WITH_TIMEOUT(reply->state(), DockerReply::State::Failed, 10000);
    QCOMPARE(reply->error().kind(), DockerError::Kind::PreconditionFailed);
    reply->deleteLater();
}

void HttpRequestBodyTest::reportsErrorWhenServerClosesEarly()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("context.tar"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QByteArray(64 * 1024, 'x'));
    file.close();

    m_closeEarly = true;
    DockerClient client;
    client.setEndpoint(DockerEndpoint::unixSocket(m_socketPath));
    DockerReply *reply = client.postFile(QStringLiteral("/build"),
                                         QUrlQuery(),
                                         path,
                                         QByteArrayLiteral("application/x-tar"),
                                         5000,
                                         5000);
    QSignalSpy finishedSpy(reply, &DockerReply::finished);
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);
    QCOMPARE(reply->state(), DockerReply::State::Failed);
    reply->deleteLater();
}

void HttpRequestBodyTest::cancelStopsTheUpload()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("context.tar"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QByteArray(512 * 1024, 'y'));
    file.close();

    DockerClient client;
    client.setEndpoint(DockerEndpoint::unixSocket(m_socketPath));
    DockerReply *reply = client.postFile(QStringLiteral("/build"),
                                         QUrlQuery(),
                                         path,
                                         QByteArrayLiteral("application/x-tar"),
                                         5000,
                                         5000);
    QSignalSpy finishedSpy(reply, &DockerReply::finished);
    reply->cancel();
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);
    QCOMPARE(reply->state(), DockerReply::State::Cancelled);
    reply->deleteLater();
}

QTEST_MAIN(HttpRequestBodyTest)

#include "tst_http_request_body.moc"
