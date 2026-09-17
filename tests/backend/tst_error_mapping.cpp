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
    void conflictsExplainWhatToDo();
    void categorisesErrorsForTheUi();
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
        DockerError::Kind::Conflict,
        DockerError::Kind::PreconditionFailed,
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

/*!
 * 409 的三类语义必须能区分（ARCH_V4 §2.2.2）：引擎只在 message 里说明原因，
 * 而用户需要知道下一步做什么。
 */
void ErrorMappingTest::conflictsExplainWhatToDo()
{
    const DockerError running = DockerError::fromHttpStatus(
        409,
        QStringLiteral("You cannot remove a running container 1111. Stop the container before attempting removal or force remove"));
    QCOMPARE(running.kind(), DockerError::Kind::Conflict);

    const DockerError inUse =
        DockerError::fromHttpStatus(409, QStringLiteral("conflict: unable to delete 1a2b (cannot be forced) - image is being used by running container 3c4d"));
    const DockerError multipleTags =
        DockerError::fromHttpStatus(409, QStringLiteral("conflict: unable to delete 1a2b (must be forced) - image is referenced in multiple repositories"));

    const QString runningText = dockerErrorText(running);
    const QString inUseText = dockerErrorText(inUse);
    const QString tagsText = dockerErrorText(multipleTags);
    QVERIFY(!runningText.isEmpty() && !inUseText.isEmpty() && !tagsText.isEmpty());
    QVERIFY2(runningText != inUseText && inUseText != tagsText && runningText != tagsText,
             "the three 409 meanings must not collapse into one sentence");

    // 未知措辞退化为通用文案，而不是空字符串
    const DockerError unknown = DockerError::fromHttpStatus(409, QStringLiteral("something new"));
    QVERIFY(!dockerErrorText(unknown).isEmpty());
}

/*!
 * 错误分级（ARCH_V4 §2.2.2）：分级决定 UI 用警告还是错误样式，
 * 因此它必须与所解决的「谁的错」一致。
 */
void ErrorMappingTest::categorisesErrorsForTheUi()
{
    QCOMPARE(dockerErrorCategoryKey(DockerError::Kind::PermissionDenied), QStringLiteral("userActionable"));
    QCOMPARE(dockerErrorCategoryKey(DockerError::Kind::Conflict), QStringLiteral("userActionable"));
    QCOMPARE(dockerErrorCategoryKey(DockerError::Kind::NotFound), QStringLiteral("userActionable"));
    QCOMPARE(dockerErrorCategoryKey(DockerError::Kind::DockerUnavailable), QStringLiteral("environment"));
    QCOMPARE(dockerErrorCategoryKey(DockerError::Kind::EngineError), QStringLiteral("environment"));
    QCOMPARE(dockerErrorCategoryKey(DockerError::Kind::UnexpectedPayload), QStringLiteral("unexpected"));
    QCOMPARE(dockerErrorCategoryKey(DockerError::Kind::None), QStringLiteral("none"));

    // 「目标已经消失」是唯一带引导动作的分级：刷新即可恢复
    QCOMPARE(dockerErrorActionKey(DockerError(DockerError::Kind::NotFound, QString())), QStringLiteral("refresh"));
    QVERIFY(dockerErrorActionKey(DockerError(DockerError::Kind::EngineError, QString())).isEmpty());
    QVERIFY(dockerErrorActionKey(DockerError(DockerError::Kind::PermissionDenied, QString())).isEmpty());
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
