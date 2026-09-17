/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/container_log_controller.h"
#include "support/mock_docker_backend.h"
#include "i18n.h"

#include <QSignalSpy>
#include <QtTest>

using namespace Kontainer;

/*!
 * 日志控制器（ARCH_V5_V8 §3.1.2/§3.1.4）。
 *
 * 三件事最容易出错、也最难在界面上发现，因此逐条钉死：
 *   1. 有界：行数/字节双上限，超限从头部丢并如实报数（否则日志会把 KCM 拖垮）；
 *   2. 批处理：短时间涌入的内容合并成一次刷新（否则每帧都触发布局与滚动）；
 *   3. 暂停是缓冲而不是丢弃：恢复时一次性补上，期间的内容一条不少。
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
    m_logs->setFlushIntervalMs(0); // 测试里同步刷新，断言才确定
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
    // 批处理：间隔大于 0 时，短时间涌入的多批内容合并到一次刷新
    m_logs->setFlushIntervalMs(50);
    m_logs->connectTo(QStringLiteral("cid-1"), false);
    QSignalSpy textSpy(m_logs, &ContainerLogController::textChanged);

    m_backend->emitLogLines(QStringLiteral("cid-1"), {line(QStringLiteral("a"))});
    m_backend->emitLogLines(QStringLiteral("cid-1"), {line(QStringLiteral("b"))});
    m_backend->emitLogLines(QStringLiteral("cid-1"), {line(QStringLiteral("c"))});
    QCOMPARE(textSpy.count(), 0); // 还没到刷新窗口

    QTRY_COMPARE_WITH_TIMEOUT(textSpy.count(), 1, 2000);
    QCOMPARE(m_logs->text(), QStringLiteral("a\nb\nc\n"));

    // 累计到 32 KiB 时不必等窗口：立刻刷新
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

    // 暂停期间继续接收，但**不追加到可见文本**
    m_backend->emitLogLines(QStringLiteral("cid-1"), {line(QStringLiteral("during-1"))});
    m_backend->emitLogLines(QStringLiteral("cid-1"), {line(QStringLiteral("during-2"))});
    QCOMPARE(m_logs->text(), QStringLiteral("before\n"));
    QCOMPARE(appendSpy.count(), 0);

    // 恢复：一次性补上，一条都不少
    m_logs->resume();
    QVERIFY(!m_logs->paused());
    QCOMPARE(m_logs->stateKey(), QStringLiteral("streaming"));
    QCOMPARE(m_logs->text(), QStringLiteral("before\nduring-1\nduring-2\n"));
    QCOMPARE(appendSpy.count(), 1);
}

void ContainerLogControllerTest::provisionalLinesReplaceTheLastLine()
{
    // `\r` 覆盖（进度条）：临时行替换上一行，而不是不断新增
    m_logs->connectTo(QStringLiteral("cid-1"), false);
    m_backend->emitLogLines(QStringLiteral("cid-1"), {line(QStringLiteral("10%"), false)});
    QCOMPARE(m_logs->text(), QStringLiteral("10%")); // 临时行：没有换行
    m_backend->emitLogLines(QStringLiteral("cid-1"), {line(QStringLiteral("50%"), false)});
    QCOMPARE(m_logs->text(), QStringLiteral("50%"));
    QCOMPARE(m_logs->lineCount(), 1);

    // 行完成后换行，之后的内容是新的一行
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
    // 保留的是**最新**的行（像 tail 一样）
    QVERIFY(m_logs->text().endsWith(QStringLiteral("line-%1\n").arg(ContainerLogController::kMaxLines + 24)));
    QVERIFY2(!m_logs->text().contains(QStringLiteral("line-0\n")), "oldest lines are dropped first");
}

void ContainerLogControllerTest::byteLimitTrimsEvenWhenLineCountIsSmall()
{
    // 字节上限独立生效：少量超长行也不能把内存撑爆
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
    // 自然结束（容器停止）：给"已结束"，界面据此提供重连
    m_logs->connectTo(QStringLiteral("cid-1"), false);
    m_backend->finishLogs(QStringLiteral("cid-1"), DockerBackendInterface::LogStreamEnd::Ended);
    QCOMPARE(m_logs->stateKey(), QStringLiteral("ended"));
    QVERIFY(m_logs->errorKey().isEmpty());

    // 失败：区分"容器不在了"与"日志驱动不支持读取"（后者要给替代做法）
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

    // 断开后迟到的内容不能再进控制台，也不能把状态改回 streaming
    m_backend->emitLogLines(QStringLiteral("cid-1"), {line(QStringLiteral("late"))});
    QCOMPARE(m_logs->text(), QStringLiteral("before\n"));
    QCOMPARE(m_logs->stateKey(), QStringLiteral("idle"));

    // 别的容器的内容同样要忽略
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
    // 重连 = 重新读 tail：先清空，避免把同一段历史重复显示两遍
    QVERIFY2(!m_logs->text().contains(QStringLiteral("old")), "reconnect must not duplicate history");
    QCOMPARE(m_backend->lastLogContainerId(), QStringLiteral("cid-1"));
    QVERIFY2(m_backend->lastLogTty(), "reconnect must keep the container's TTY mode");
    QCOMPARE(m_backend->lastLogTailLines(), 42);
}

QTEST_MAIN(ContainerLogControllerTest)

#include "tst_container_log_controller.moc"
