/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/docker_error.h"
#include "i18n.h"
#include "model/docker_error_text.h"

#include <QAbstractSocket>
#include <QtTest>

using namespace Kontainer;

/*! Error-mapping unit tests (ARCH_V1 §28 Error mapping / §23 layered errors). */
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
    // Note: this Qt (6.11) only runs data-driven cases shaped as a no-argument test function
    // plus QFETCH; test functions taking parameters are silently skipped, so QFETCH everywhere.
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
 * The three 409 meanings must stay distinguishable (ARCH_V4 §2.2.2): the engine states the
 * reason only in the message, while the user needs to know what to do next.
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

    // Unknown wording degrades to generic text, not an empty string
    const DockerError unknown = DockerError::fromHttpStatus(409, QStringLiteral("something new"));
    QVERIFY(!dockerErrorText(unknown).isEmpty());
}

/*!
 * Error categorisation (ARCH_V4 §2.2.2): the category decides whether the UI uses warning or
 * error styling, so it must match the underlying "whose fault" reading.
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

    // "Target is gone" is the only category with a guided action: refresh recovers it
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
