/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/container_log_controller.h"
#include "support/mock_docker_backend.h"
#include "i18n.h"

#include <QSignalSpy>
#include <QtTest>

using namespace Kontainer;

/*!
 * Log controller (ARCH_V5_V8 §3.1.2/§3.1.4).
 *
 * Three things break easily and are hard to spot in the UI, so each is pinned down:
 *   1. Bounded: line and byte caps; overflow drops from the head and reports the count (an
 *      unbounded log can drag the whole KCM down);
 *   2. Batched: a burst collapses into one refresh (otherwise every frame relayouts and scrolls);
 *   3. Pause buffers instead of discarding: resume replays everything, losing no line.
 */
class ContainerLogControllerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();

    void connectStartsStreamAndReportsState();
    void linesAreBatchedIntoOneUpdate();
    void pauseBuffersAndResumeCatchesUp();
    void provisionalLinesReplaceTheLastLine();
    void trimmingKeepsTheNewestLinesAndCountsDrops();
    void byteLimitTrimsEvenWhenLineCountIsSmall();
    void finishedStatesAreDistinguished();
    void disconnectStopsTheStreamAndIgnoresLateChunks();
    void reconnectRestartsFromTheTail();

private:
    MockDockerBackend *m_backend = nullptr;
    ContainerLogController *m_logs = nullptr;
};

namespace
{
LogLine line(const QString &text, bool complete = true, LogLine::Stream stream = LogLine::Stream::Stdout)
{
    LogLine result;
    result.text = text;
    result.complete = complete;
    result.stream = stream;
    return result;
}
} // namespace

void ContainerLogControllerTest::initTestCase()
{
    setupTranslationDomain();
    qRegisterMetaType<Kontainer::LogLine>("Kontainer::LogLine");
    qRegisterMetaType<QList<Kontainer::LogLine>>("QList<Kontainer::LogLine>");
    qRegisterMetaType<Kontainer::DockerBackendInterface::LogStreamEnd>("Kontainer::DockerBackendInterface::LogStreamEnd");
}

void ContainerLogControllerTest::init()
{
    delete m_logs;
    delete m_backend;
    m_backend = new MockDockerBackend(this);
    m_logs = new ContainerLogController(m_backend, this);
    m_logs->setFlushIntervalMs(0); // synchronous flush in tests, so assertions are deterministic
}

void ContainerLogControllerTest::connectStartsStreamAndReportsState()
{
    QCOMPARE(m_logs->stateKey(), QStringLiteral("idle"));
    m_logs->connectTo(QStringLiteral("cid-1"), false, 150);
    QCOMPARE(m_logs->stateKey(), QStringLiteral("connecting"));
    QCOMPARE(m_backend->lastLogContainerId(), QStringLiteral("cid-1"));
    QVERIFY2(!m_backend->lastLogTty(), "the TTY flag must come from the container detail");
    QVERIFY(m_backend->lastLogFollow());
    QCOMPARE(m_backend->lastLogTailLines(), 150);

    m_backend->emitLogLines(QStringLiteral("cid-1"), {line(QStringLiteral("hello"))});
    QCOMPARE(m_logs->stateKey(), QStringLiteral("streaming"));
    QCOMPARE(m_logs->text(), QStringLiteral("hello\n"));
    QCOMPARE(m_logs->lineCount(), 1);
}

void ContainerLogControllerTest::linesAreBatchedIntoOneUpdate()
{
    // Batching: with a non-zero interval, several bursts merge into a single refresh
    m_logs->setFlushIntervalMs(50);
    m_logs->connectTo(QStringLiteral("cid-1"), false);
    QSignalSpy textSpy(m_logs, &ContainerLogController::textChanged);

    m_backend->emitLogLines(QStringLiteral("cid-1"), {line(QStringLiteral("a"))});
    m_backend->emitLogLines(QStringLiteral("cid-1"), {line(QStringLiteral("b"))});
    m_backend->emitLogLines(QStringLiteral("cid-1"), {line(QStringLiteral("c"))});
    QCOMPARE(textSpy.count(), 0); // still inside the flush window

    QTRY_COMPARE_WITH_TIMEOUT(textSpy.count(), 1, 2000);
    QCOMPARE(m_logs->text(), QStringLiteral("a\nb\nc\n"));

    // At 32 KiB accumulated the window is bypassed: flush immediately
    m_logs->setFlushIntervalMs(60000);
    const QString big = QString(40000, QLatin1Char('x'));
    m_backend->emitLogLines(QStringLiteral("cid-1"), {line(big)});
    QCOMPARE(m_logs->lineCount(), 4);
}

void ContainerLogControllerTest::pauseBuffersAndResumeCatchesUp()
{
    m_logs->connectTo(QStringLiteral("cid-1"), false);
    m_backend->emitLogLines(QStringLiteral("cid-1"), {line(QStringLiteral("before"))});
    QCOMPARE(m_logs->text(), QStringLiteral("before\n"));

    QSignalSpy appendSpy(m_logs, &ContainerLogController::appended);
    m_logs->pause();
    QVERIFY(m_logs->paused());
    QCOMPARE(m_logs->stateKey(), QStringLiteral("paused"));
    QCOMPARE(appendSpy.count(), 0);

    // While paused we keep receiving, but never append to the visible text
    m_backend->emitLogLines(QStringLiteral("cid-1"), {line(QStringLiteral("during-1"))});
    m_backend->emitLogLines(QStringLiteral("cid-1"), {line(QStringLiteral("during-2"))});
    QCOMPARE(m_logs->text(), QStringLiteral("before\n"));
    QCOMPARE(appendSpy.count(), 0);

    // Resume replays everything at once, not one line missing
    m_logs->resume();
    QVERIFY(!m_logs->paused());
    QCOMPARE(m_logs->stateKey(), QStringLiteral("streaming"));
    QCOMPARE(m_logs->text(), QStringLiteral("before\nduring-1\nduring-2\n"));
    QCOMPARE(appendSpy.count(), 1);
}

void ContainerLogControllerTest::provisionalLinesReplaceTheLastLine()
{
    // `\r` overwrite (progress bars): a provisional line replaces the previous one, not appends
    m_logs->connectTo(QStringLiteral("cid-1"), false);
    m_backend->emitLogLines(QStringLiteral("cid-1"), {line(QStringLiteral("10%"), false)});
    QCOMPARE(m_logs->text(), QStringLiteral("10%")); // provisional line: no trailing newline
    m_backend->emitLogLines(QStringLiteral("cid-1"), {line(QStringLiteral("50%"), false)});
    QCOMPARE(m_logs->text(), QStringLiteral("50%"));
    QCOMPARE(m_logs->lineCount(), 1);

    // A completed line gets its newline; later output is a new line
    m_backend->emitLogLines(QStringLiteral("cid-1"), {line(QStringLiteral("100% done"), true)});
    QCOMPARE(m_logs->text(), QStringLiteral("100% done\n"));
    m_backend->emitLogLines(QStringLiteral("cid-1"), {line(QStringLiteral("next"))});
    QCOMPARE(m_logs->text(), QStringLiteral("100% done\nnext\n"));
    QCOMPARE(m_logs->lineCount(), 2);
}

void ContainerLogControllerTest::trimmingKeepsTheNewestLinesAndCountsDrops()
{
    m_logs->connectTo(QStringLiteral("cid-1"), false);
    QList<LogLine> bulk;
    for (int i = 0; i < ContainerLogController::kMaxLines + 25; ++i) {
        bulk.append(line(QStringLiteral("line-%1").arg(i)));
    }
    m_backend->emitLogLines(QStringLiteral("cid-1"), bulk);

    QCOMPARE(m_logs->lineCount(), ContainerLogController::kMaxLines);
    QCOMPARE(m_logs->droppedLineCount(), 25);
    // The newest lines are kept, tail-style
    QVERIFY(m_logs->text().endsWith(QStringLiteral("line-%1\n").arg(ContainerLogController::kMaxLines + 24)));
    QVERIFY2(!m_logs->text().contains(QStringLiteral("line-0\n")), "oldest lines are dropped first");
}

void ContainerLogControllerTest::byteLimitTrimsEvenWhenLineCountIsSmall()
{
    // The byte cap applies on its own: a few huge lines must not blow up memory
    m_logs->connectTo(QStringLiteral("cid-1"), false);
    const QString huge(300 * 1024, QLatin1Char('y'));
    m_backend->emitLogLines(QStringLiteral("cid-1"), {line(huge)});
    m_backend->emitLogLines(QStringLiteral("cid-1"), {line(huge)});
    m_backend->emitLogLines(QStringLiteral("cid-1"), {line(QStringLiteral("tail"))});

    QVERIFY2(m_logs->lineCount() < 3, "the byte limit must drop older lines");
    QVERIFY(m_logs->droppedLineCount() >= 1);
    QVERIFY(m_logs->text().endsWith(QStringLiteral("tail\n")));
    QVERIFY2(m_logs->text().size() <= ContainerLogController::kMaxBytes + 4096, "the buffer must stay bounded");
}

void ContainerLogControllerTest::finishedStatesAreDistinguished()
{
    // Natural end (container stopped) reports "ended", on which the UI offers reconnect
    m_logs->connectTo(QStringLiteral("cid-1"), false);
    m_backend->finishLogs(QStringLiteral("cid-1"), DockerBackendInterface::LogStreamEnd::Ended);
    QCOMPARE(m_logs->stateKey(), QStringLiteral("ended"));
    QVERIFY(m_logs->errorKey().isEmpty());

    // Failure: distinguish "container is gone" from an unreadable logging driver (needs fallback)
    m_logs->connectTo(QStringLiteral("cid-2"), false);
    m_backend->finishLogs(QStringLiteral("cid-2"),
                          DockerBackendInterface::LogStreamEnd::Failed,
                          DockerError(DockerError::Kind::NotFound, QStringLiteral("No such container")));
    QCOMPARE(m_logs->stateKey(), QStringLiteral("failed"));
    QCOMPARE(m_logs->errorKey(), QStringLiteral("containerGone"));

    m_logs->connectTo(QStringLiteral("cid-3"), false);
    m_backend->finishLogs(QStringLiteral("cid-3"),
                          DockerBackendInterface::LogStreamEnd::Failed,
                          DockerError(DockerError::Kind::EngineError, QStringLiteral("configured logging driver does not support reading")));
    QCOMPARE(m_logs->errorKey(), QStringLiteral("driverUnsupported"));
}

void ContainerLogControllerTest::disconnectStopsTheStreamAndIgnoresLateChunks()
{
    m_logs->connectTo(QStringLiteral("cid-1"), false);
    m_backend->emitLogLines(QStringLiteral("cid-1"), {line(QStringLiteral("before"))});
    m_logs->disconnect();
    QCOMPARE(m_logs->stateKey(), QStringLiteral("idle"));
    QCOMPARE(m_backend->stopLogsCount(QStringLiteral("cid-1")), 1);

    // Late chunks after disconnect must not reach the console or set the state back to streaming
    m_backend->emitLogLines(QStringLiteral("cid-1"), {line(QStringLiteral("late"))});
    QCOMPARE(m_logs->text(), QStringLiteral("before\n"));
    QCOMPARE(m_logs->stateKey(), QStringLiteral("idle"));

    // Another container's chunks are ignored too
    m_logs->connectTo(QStringLiteral("cid-2"), false);
    m_backend->emitLogLines(QStringLiteral("cid-1"), {line(QStringLiteral("someone-else"))});
    QVERIFY2(!m_logs->text().contains(QStringLiteral("someone-else")), "another container's logs must not leak in");
}

void ContainerLogControllerTest::reconnectRestartsFromTheTail()
{
    m_logs->connectTo(QStringLiteral("cid-1"), true, 42);
    m_backend->emitLogLines(QStringLiteral("cid-1"), {line(QStringLiteral("old"))});
    QVERIFY(m_logs->text().contains(QStringLiteral("old")));

    m_logs->reconnect();
    // Reconnect re-reads the tail: clear first, so history is not shown twice
    QVERIFY2(!m_logs->text().contains(QStringLiteral("old")), "reconnect must not duplicate history");
    QCOMPARE(m_backend->lastLogContainerId(), QStringLiteral("cid-1"));
    QVERIFY2(m_backend->lastLogTty(), "reconnect must keep the container's TTY mode");
    QCOMPARE(m_backend->lastLogTailLines(), 42);
}

QTEST_MAIN(ContainerLogControllerTest)

#include "tst_container_log_controller.moc"
