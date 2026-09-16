/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/docker_error.h"
#include "i18n.h"
#include "model/docker_error_text.h"

#include <QAbstractSocket>
#include <QtTest>

using namespace Kontainer;

/*! 错误映射单元测试（ARCH_V1 §28 Error mapping / §23 分层错误）。 */
class ErrorMappingTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void mapsHttpStatusCodes_data();
    void mapsHttpStatusCodes();

    void mapsSocketErrors_data();
    void mapsSocketErrors();

    void everyErrorHasUserText();
    void detailIsPreserved();
};

void ErrorMappingTest::initTestCase()
{
    setupTranslationDomain();
}

void ErrorMappingTest::mapsHttpStatusCodes_data()
{
    QTest::addColumn<int>("status");
    QTest::addColumn<int>("expectedKind");

    QTest::newRow("401 -> PermissionDenied") << 401 << int(DockerError::Kind::PermissionDenied);
    QTest::newRow("403 -> PermissionDenied") << 403 << int(DockerError::Kind::PermissionDenied);
    QTest::newRow("404 -> NotFound") << 404 << int(DockerError::Kind::NotFound);
    QTest::newRow("400 -> HttpError") << 400 << int(DockerError::Kind::HttpError);
    QTest::newRow("500 -> EngineError") << 500 << int(DockerError::Kind::EngineError);
}

void ErrorMappingTest::mapsHttpStatusCodes()
{
    // 注意：这台 Qt（6.11）只执行「无参测试函数 + QFETCH」形式的数据驱动用例，
    // 带参数的测试函数会被静默跳过，因此统一使用 QFETCH。
    QFETCH(int, status);
    QFETCH(int, expectedKind);

    const DockerError error = DockerError::fromHttpStatus(status, QStringLiteral("api message"));
    QCOMPARE(int(error.kind()), expectedKind);
    QCOMPARE(error.httpStatus(), status);
    QVERIFY(error.isError());
    QVERIFY(!dockerErrorText(error).isEmpty());
}

void ErrorMappingTest::mapsSocketErrors_data()
{
    QTest::addColumn<int>("socketError");
    QTest::addColumn<int>("expectedKind");

    QTest::newRow("ConnectionRefused -> DockerUnavailable")
        << int(QAbstractSocket::ConnectionRefusedError) << int(DockerError::Kind::DockerUnavailable);
    QTest::newRow("SocketAccess -> PermissionDenied")
        << int(QAbstractSocket::SocketAccessError) << int(DockerError::Kind::PermissionDenied);
    QTest::newRow("Timeout -> Timeout") << int(QAbstractSocket::SocketTimeoutError) << int(DockerError::Kind::Timeout);
    QTest::newRow("RemoteHostClosed -> ConnectionFailed")
        << int(QAbstractSocket::RemoteHostClosedError) << int(DockerError::Kind::ConnectionFailed);
}

void ErrorMappingTest::mapsSocketErrors()
{
    QFETCH(int, socketError);
    QFETCH(int, expectedKind);

    const DockerError error = DockerError::fromSocketError(socketError, QStringLiteral("socket error"));
    QCOMPARE(int(error.kind()), expectedKind);
    QVERIFY(error.isError());
}

void ErrorMappingTest::everyErrorHasUserText()
{
    const QList<DockerError::Kind> kinds = {
        DockerError::Kind::DockerUnavailable,
        DockerError::Kind::ConnectionFailed,
        DockerError::Kind::PermissionDenied,
        DockerError::Kind::Timeout,
        DockerError::Kind::ApiVersionMismatch,
        DockerError::Kind::NotFound,
        DockerError::Kind::HttpError,
        DockerError::Kind::EngineError,
        DockerError::Kind::InvalidResponse,
        DockerError::Kind::UnexpectedPayload,
    };
    for (DockerError::Kind kind : kinds) {
        const DockerError error(kind, QStringLiteral("detail"));
        QVERIFY2(!dockerErrorText(error).isEmpty(), "each error kind must be user visible");
    }
}

void ErrorMappingTest::detailIsPreserved()
{
    const DockerError error(DockerError::Kind::HttpError, QStringLiteral("client sent an HTTP request to an HTTPS server"));
    QCOMPARE(error.detail(), QStringLiteral("client sent an HTTP request to an HTTPS server"));
    QCOMPARE(int(DockerError::Kind::None), int(DockerError().kind()));
    QVERIFY(!DockerError().isError());
}

QTEST_GUILESS_MAIN(ErrorMappingTest)

#include "tst_error_mapping.moc"
