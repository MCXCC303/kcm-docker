/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "domain/container_stats.h"
#include "model/metrics_model.h"
#include "refresh_policy.h"
#include "support/mock_docker_backend.h"

#include <QtTest>

using namespace Kontainer;

/*!
 * 资源统计测试（ARCH_V2 §17–§21/§44）。
 *
 * 统计公式属于业务逻辑（§18），因此必须在 C++ 侧被验证：
 * 首个采样、第二个采样、零增量、计数器回绕、缺失内存上限、畸形数据。
 */
class MetricsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // --- ContainerStats（纯计算） ---
    void computesCpuPercent();
    void cpuPercentHandlesZeroDeltaAndRollback();
    void cpuPercentNeedsOnlineCpus();
    void memoryUsedSubtractsPageCache();
    void memoryPercentRequiresRealLimit();

    // --- MetricsModel（采样生命周期 + 速率） ---
    void firstSampleHasNoRates();
    void secondSampleComputesRates();
    void zeroDeltaYieldsZeroRate();
    void counterRollbackYieldsUnknownRate();
    void historyIsBounded();
    void stopReleasesHistoryAndSampling();
    void repeatedFailuresStopSampling();
    void memoryLimitEqualToHostMeansUnlimited();
};

namespace
{

ContainerStats makeStats(quint64 cpuTotal, quint64 systemTotal, quint64 cpuPre, quint64 systemPre, int cpus)
{
    ContainerStats stats;
    stats.containerId = QStringLiteral("cid");
    stats.timestamp = QDateTime::currentDateTimeUtc();
    stats.cpuTotalUsage = cpuTotal;
    stats.systemCpuUsage = systemTotal;
    stats.cpuPreTotalUsage = cpuPre;
    stats.systemPreCpuUsage = systemPre;
    stats.onlineCpus = cpus;
    return stats;
}

} // namespace

void MetricsTest::computesCpuPercent()
{
    // (1e9 / 1e11) × 4 × 100 = 4%
    const ContainerStats stats = makeStats(3'000'000'000ULL, 600'000'000'000ULL, 2'000'000'000ULL, 500'000'000'000ULL, 4);
    QVERIFY(qAbs(stats.cpuPercent() - 4.0) < 0.001);
}

void MetricsTest::cpuPercentHandlesZeroDeltaAndRollback()
{
    // systemDelta == 0：不能除零，也不能给 NaN
    const ContainerStats zero = makeStats(100, 500, 100, 500, 4);
    QCOMPARE(zero.cpuPercent(), 0.0);

    // 计数器回绕（容器重启后 cpu_total 变小）：返回 0 而不是负数/天文数字
    const ContainerStats rollback = makeStats(1000, 500, 2000, 400, 4);
    QCOMPARE(rollback.cpuPercent(), 0.0);
}

void MetricsTest::cpuPercentNeedsOnlineCpus()
{
    const ContainerStats stats = makeStats(3'000'000'000ULL, 600'000'000'000ULL, 2'000'000'000ULL, 500'000'000'000ULL, 0);
    QCOMPARE(stats.cpuPercent(), 0.0);
}

void MetricsTest::memoryUsedSubtractsPageCache()
{
    ContainerStats stats;
    stats.memoryUsageBytes = 110'000'000;
    stats.memoryCacheBytes = 10'000'000;
    QCOMPARE(stats.memoryUsedBytes(), quint64(100'000'000));

    // cache 大于 usage（异常数据）时不能出现负数
    stats.memoryUsageBytes = 1000;
    stats.memoryCacheBytes = 5000;
    QCOMPARE(stats.memoryUsedBytes(), quint64(0));
}

void MetricsTest::memoryPercentRequiresRealLimit()
{
    ContainerStats stats;
    stats.memoryUsageBytes = 100;
    stats.memoryLimitBytes = 0;
    QCOMPARE(stats.memoryPercent(), -1.0); // 无上限：不给虚假百分比（§19）

    stats.memoryLimitBytes = 400;
    QVERIFY(qAbs(stats.memoryPercent() - 25.0) < 0.001);
}

void MetricsTest::firstSampleHasNoRates()
{
    MockDockerBackend backend;
    MetricsModel metrics;
    metrics.setBackend(&backend);

    metrics.start(QStringLiteral("cid"));
    QVERIFY(metrics.sampling());
    QVERIFY(!metrics.hasData());
    QCOMPARE(metrics.sampleCount(), 0);

    ContainerStats stats = makeStats(1'000'000'000ULL, 10'000'000'000ULL, 900'000'000ULL, 9'000'000'000ULL, 4);
    stats.networkRxBytes = 1000;
    stats.networkTxBytes = 2000;
    metrics.addSample(stats);

    QCOMPARE(metrics.sampleCount(), 1);
    QVERIFY(metrics.hasData());
    // 首个采样无法计算速率：必须是“未知”而不是 0（§20/§44）
    QCOMPARE(metrics.networkRxPerSecond(), -1.0);
    QCOMPARE(metrics.networkTxPerSecond(), -1.0);
    QCOMPARE(metrics.blockReadPerSecond(), -1.0);
    QVERIFY(metrics.cpuPercent() > 0.0);
}

void MetricsTest::secondSampleComputesRates()
{
    MockDockerBackend backend;
    MetricsModel metrics;
    metrics.setBackend(&backend);
    metrics.start(QStringLiteral("cid"));

    ContainerStats first = makeStats(1'000'000'000ULL, 10'000'000'000ULL, 900'000'000ULL, 9'000'000'000ULL, 4);
    first.timestamp = QDateTime::currentDateTimeUtc().addSecs(-5);
    first.networkRxBytes = 1'000'000;
    first.networkTxBytes = 2'000'000;
    metrics.addSample(first);

    ContainerStats second = makeStats(1'000'000'000ULL, 10'000'000'000ULL, 900'000'000ULL, 9'000'000'000ULL, 4);
    second.timestamp = first.timestamp.addSecs(5); // 5 秒后
    second.networkRxBytes = 1'500'000; // +500000 bytes / 5s = 100000 B/s
    second.networkTxBytes = 2'000'000;
    metrics.addSample(second);

    QCOMPARE(metrics.sampleCount(), 2);
    QVERIFY(qAbs(metrics.networkRxPerSecond() - 100000.0) < 1.0);
    QCOMPARE(metrics.networkTxPerSecond(), 0.0); // 零增量是合法的 0，不是未知
}

void MetricsTest::zeroDeltaYieldsZeroRate()
{
    MockDockerBackend backend;
    MetricsModel metrics;
    metrics.setBackend(&backend);
    metrics.start(QStringLiteral("cid"));

    ContainerStats first = makeStats(1, 10, 0, 0, 4);
    first.timestamp = QDateTime::currentDateTimeUtc().addSecs(-5);
    first.blockReadBytes = 4096;
    metrics.addSample(first);

    ContainerStats second = first;
    second.timestamp = first.timestamp.addSecs(5);
    metrics.addSample(second);

    QCOMPARE(metrics.blockReadPerSecond(), 0.0);
}

void MetricsTest::counterRollbackYieldsUnknownRate()
{
    MockDockerBackend backend;
    MetricsModel metrics;
    metrics.setBackend(&backend);
    metrics.start(QStringLiteral("cid"));

    ContainerStats first = makeStats(1, 10, 0, 0, 4);
    first.timestamp = QDateTime::currentDateTimeUtc().addSecs(-5);
    first.blockWriteBytes = 10'000;
    metrics.addSample(first);

    ContainerStats second = first;
    second.timestamp = first.timestamp.addSecs(5);
    second.blockWriteBytes = 500; // 容器重启导致计数器回绕
    metrics.addSample(second);

    QCOMPARE(metrics.blockWritePerSecond(), -1.0);
}

void MetricsTest::historyIsBounded()
{
    MockDockerBackend backend;
    MetricsModel metrics;
    metrics.setBackend(&backend);
    metrics.start(QStringLiteral("cid"));

    for (int i = 0; i < RefreshPolicy::kMetricsHistorySamples * 2; ++i) {
        ContainerStats stats = makeStats(quint64(i) * 1000, 1'000'000, 0, 0, 4);
        stats.timestamp = QDateTime::currentDateTimeUtc().addMSecs(i);
        metrics.addSample(stats);
    }

    QCOMPARE(metrics.sampleCount(), RefreshPolicy::kMetricsHistorySamples);
    QCOMPARE(metrics.cpuHistory().size(), RefreshPolicy::kMetricsHistorySamples);
}

void MetricsTest::stopReleasesHistoryAndSampling()
{
    MockDockerBackend backend;
    MetricsModel metrics;
    metrics.setBackend(&backend);
    metrics.start(QStringLiteral("cid"));

    ContainerStats stats = makeStats(1000, 1'000'000, 0, 0, 4);
    metrics.addSample(stats);
    QCOMPARE(metrics.sampleCount(), 1);
    QVERIFY(backend.samplingIds().contains(QStringLiteral("cid")));

    metrics.stop();

    QVERIFY(!metrics.sampling());
    QCOMPARE(metrics.sampleCount(), 0);
    QVERIFY(metrics.cpuHistory().isEmpty());
    // 离开详情页后 backend 侧也不再需要采样（§27）
    QVERIFY(backend.samplingIds().isEmpty());
}

void MetricsTest::repeatedFailuresStopSampling()
{
    MockDockerBackend backend;
    MetricsModel metrics;
    metrics.setBackend(&backend);
    metrics.start(QStringLiteral("cid"));
    QVERIFY(metrics.sampling());

    // 容器在打开详情页后停止：连续失败达阈值后自动停止采样
    for (int i = 0; i < 3; ++i) {
        metrics.noteFailure();
    }
    QVERIFY(!metrics.sampling());
}

void MetricsTest::memoryLimitEqualToHostMeansUnlimited()
{
    MockDockerBackend backend;
    EngineInfo engine;
    engine.available = true;
    engine.memoryTotalBytes = 32LL * 1024 * 1024 * 1024; // 宿主 32 GiB
    backend.setEngineInfo(engine);

    MetricsModel metrics;
    metrics.setBackend(&backend);
    metrics.start(QStringLiteral("cid"));

    ContainerStats stats = makeStats(1, 10, 0, 0, 4);
    stats.memoryUsageBytes = 100'000'000;
    stats.memoryLimitBytes = quint64(engine.memoryTotalBytes); // Docker 在无限制时报宿主内存
    metrics.addSample(stats);

    QVERIFY(!metrics.memoryLimitEffective());
    QCOMPARE(metrics.memoryPercent(), -1.0); // 不显示虚假百分比（§19）
}

QTEST_GUILESS_MAIN(MetricsTest)

#include "tst_metrics.moc"
