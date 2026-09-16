/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/docker_backend.h"
#include "i18n.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QTemporaryDir>
#include <QtTest>

using namespace Kontainer;

/*!
 * 用一个进程内的假 Docker Engine（Unix socket + 最小 HTTP）驱动真实 DockerBackend。
 *
 * 覆盖真实 daemon 上难以复现的行为：
 *  - 请求去重（§17：连续刷新只产生一个请求）
 *  - /info 刷新失败后旧计数必须作废（不能把过期计数当当前值）
 *  - API 版本协商失败 / daemon 返回 400 "too new"
 *  - Content-Length 与 chunked 两种响应
 *
 * 只读：假 Engine 也只实现 GET。
 */
class FakeEngine : public QObject
{
    Q_OBJECT

public:
    explicit FakeEngine(QObject *parent = nullptr)
        : QObject(parent)
    {
        connect(&m_server, &QLocalServer::newConnection, this, &FakeEngine::handleConnections);
    }

    bool listen()
    {
        m_socketPath = m_directory.path() + QStringLiteral("/fake-docker.sock");
        QLocalServer::removeServer(m_socketPath);
        return m_server.listen(m_socketPath);
    }

    QString socketPath() const
    {
        return m_socketPath;
    }

    void setFailInfo(bool fail)
    {
        m_failInfo = fail;
    }
    void setApiVersion(const QString &version)
    {
        m_apiVersion = version;
    }
    /*! 让某个裸路径返回指定状态码（用于伪造 400 version 错误）。 */
    void setPathStatus(const QString &barePath, int status, const QByteArray &body)
    {
        m_overrides.insert(barePath, {status, body});
    }

    int requestCount(const QString &path) const
    {
        return m_counts.value(path);
    }

    /*! 用例之间清空注入的失败/覆盖与计数，避免状态泄漏。 */
    void reset()
    {
        m_overrides.clear();
        m_counts.clear();
        m_apiVersion = QStringLiteral("1.56");
        m_failInfo = false;
    }

private:
    void handleConnections()
    {
        while (QLocalSocket *socket = m_server.nextPendingConnection()) {
            auto *buffer = new QByteArray;
            connect(socket, &QLocalSocket::readyRead, this, [this, socket, buffer] {
                buffer->append(socket->readAll());
                const int headerEnd = buffer->indexOf("\r\n\r\n");
                if (headerEnd < 0) {
                    return;
                }
                const QByteArray requestLine = buffer->left(buffer->indexOf("\r\n"));
                const QList<QByteArray> parts = requestLine.split(' ');
                if (parts.size() < 2) {
                    socket->disconnectFromServer();
                    return;
                }
                respond(socket, QString::fromLatin1(parts.at(1)));
            });
            connect(socket, &QLocalSocket::disconnected, socket, &QLocalSocket::deleteLater);
        }
    }

    static QString withoutVersionPrefix(const QString &path)
    {
        const QStringList parts = path.split(QLatin1Char('/'));
        if (parts.size() > 1 && parts.at(1).startsWith(QLatin1String("v1."))) {
            return QLatin1Char('/') + parts.mid(2).join(QLatin1Char('/'));
        }
        return path;
    }

    void respond(QLocalSocket *socket, const QString &rawPath)
    {
        const QString path = rawPath.section(QLatin1Char('?'), 0, 0);
        m_counts[path] += 1;
        const QString bare = withoutVersionPrefix(path);

        if (const auto override = m_overrides.constFind(bare); override != m_overrides.constEnd()) {
            writeResponse(socket, override->first, override->second, false);
            return;
        }

        if (bare == QLatin1String("/_ping")) {
            writeResponse(socket, 200, QByteArrayLiteral("OK"), false, QByteArrayLiteral("text/plain; charset=utf-8"));
        } else if (bare == QLatin1String("/version")) {
            const QByteArray body = QByteArrayLiteral("{\"Version\":\"99.0.0-fake\",\"ApiVersion\":\"") + m_apiVersion.toLatin1()
                + QByteArrayLiteral("\",\"MinAPIVersion\":\"1.40\",\"Os\":\"linux\",\"Arch\":\"amd64\"}");
            writeResponse(socket, 200, body, true);
        } else if (bare == QLatin1String("/info")) {
            if (m_failInfo) {
                writeResponse(socket, 500, QByteArrayLiteral("{\"message\":\"fake info failure\"}"), false);
            } else {
                writeResponse(socket, 200, infoPayload(), true);
            }
        } else if (bare == QLatin1String("/containers/json")) {
            writeResponse(socket, 200, containersPayload(), true);
        } else if (bare == QLatin1String("/images/json")) {
            writeResponse(socket, 200, imagesPayload(), true);
        } else if (bare == QLatin1String("/system/df")) {
            writeResponse(socket, 200, storagePayload(), true);
        } else if (bare.startsWith(QLatin1String("/containers/")) && bare.endsWith(QLatin1String("/json"))) {
            writeResponse(socket, 200, containerInspectPayload(), true);
        } else if (bare.startsWith(QLatin1String("/containers/")) && bare.endsWith(QLatin1String("/stats"))) {
            writeResponse(socket, 200, statsPayload(), true);
        } else {
            writeResponse(socket, 404, QByteArrayLiteral("{\"message\":\"fake engine: not found\"}"), false);
        }
    }

    static QByteArray infoPayload()
    {
        return QByteArrayLiteral("{\"Name\":\"fake-engine\",\"OperatingSystem\":\"Fake Linux\",\"OSType\":\"linux\","
                                 "\"Architecture\":\"x86_64\",\"KernelVersion\":\"0.0.0-fake\",\"CgroupVersion\":\"2\","
                                 "\"Driver\":\"overlayfs\",\"Containers\":2,\"ContainersRunning\":1,\"ContainersPaused\":0,"
                                 "\"ContainersStopped\":1,\"Images\":1,\"NCPU\":1,\"MemTotal\":1024}");
    }

    static QByteArray containersPayload()
    {
        return QByteArrayLiteral("[{\"Id\":\"1111111111111111111111111111111111111111111111111111111111111111\","
                                 "\"Names\":[\"/fake-running\"],\"Image\":\"alpine:latest\",\"State\":\"running\","
                                 "\"Status\":\"Up 2 hours\",\"Created\":1789500000},"
                                 "{\"Id\":\"2222222222222222222222222222222222222222222222222222222222222222\","
                                 "\"Names\":[\"/fake-exited\"],\"Image\":\"alpine:latest\",\"State\":\"exited\","
                                 "\"Status\":\"Exited (0) 5 minutes ago\",\"Created\":1789500000}]");
    }

    static QByteArray storagePayload()
    {
        return QByteArrayLiteral("{\"LayersSize\":46649727396,\"ImageUsage\":49400000000,\"ContainerUsage\":20340000,"
                                 "\"VolumeUsage\":0,\"BuildCacheUsage\":0,"
                                 "\"Images\":[{\"Id\":\"sha256:aaaa\",\"Size\":7700000}],"
                                 "\"Containers\":[{\"Id\":\"1111\",\"SizeRw\":20340000}],"
                                 "\"Volumes\":[],\"BuildCache\":[]}");
    }

    static QByteArray containerInspectPayload()
    {
        return QByteArrayLiteral("{"
                                 "\"Id\":\"1111111111111111111111111111111111111111111111111111111111111111\","
                                 "\"Name\":\"/fake-running\",\"Image\":\"sha256:aaaa\","
                                 "\"Created\":\"2026-09-16T11:14:22.640796684Z\","
                                 "\"Platform\":\"linux\",\"RestartCount\":1,"
                                 "\"State\":{\"Status\":\"running\",\"Running\":true,\"ExitCode\":0,\"Pid\":4242,"
                                 "\"StartedAt\":\"2026-09-16T11:14:23.000000000Z\",\"FinishedAt\":\"0001-01-01T00:00:00Z\","
                                 "\"Health\":{\"Status\":\"healthy\"}},"
                                 "\"Config\":{\"Image\":\"alpine:latest\",\"Env\":[\"PATH=/usr/bin\",\"LANG=C\"],"
                                 "\"Cmd\":[\"sleep\",\"infinity\"],\"Entrypoint\":[\"/entry.sh\"],"
                                 "\"WorkingDir\":\"/work\",\"Hostname\":\"fakehost\",\"User\":\"root\","
                                 "\"Labels\":{\"com.example.role\":\"test\"}},"
                                 "\"HostConfig\":{\"RestartPolicy\":{\"Name\":\"unless-stopped\",\"MaximumRetryCount\":0}},"
                                 "\"Mounts\":[{\"Type\":\"bind\",\"Source\":\"/host/data\",\"Destination\":\"/data\","
                                 "\"Mode\":\"rw\",\"RW\":true}],"
                                 "\"NetworkSettings\":{\"Ports\":{\"8080/tcp\":[{\"HostIp\":\"0.0.0.0\",\"HostPort\":\"18080\"}]},"
                                 "\"Networks\":{\"bridge\":{\"NetworkID\":\"net1\",\"IPAddress\":\"172.17.0.5\","
                                 "\"MacAddress\":\"02:42:ac:11:00:05\",\"Gateway\":\"172.17.0.1\"}}}}");
    }

    static QByteArray statsPayload()
    {
        // 第二个采样：与 precpu 有明显差值，便于验证 CPU 计算
        return QByteArrayLiteral("{"
                                 "\"id\":\"1111111111111111111111111111111111111111111111111111111111111111\","
                                 "\"cpu_stats\":{\"cpu_usage\":{\"total_usage\":3000000000},\"system_cpu_usage\":600000000000,"
                                 "\"online_cpus\":4},"
                                 "\"precpu_stats\":{\"cpu_usage\":{\"total_usage\":2000000000},\"system_cpu_usage\":500000000000},"
                                 "\"memory_stats\":{\"usage\":110000000,\"limit\":268435456,\"stats\":{\"inactive_file\":10000000}},"
                                 "\"networks\":{\"eth0\":{\"rx_bytes\":1000,\"tx_bytes\":2000}},"
                                 "\"blkio_stats\":{\"io_service_bytes_recursive\":[{\"op\":\"read\",\"value\":4096},"
                                 "{\"op\":\"write\",\"value\":8192}]},"
                                 "\"pids_stats\":{\"current\":3}}");
    }

    static QByteArray imagesPayload()
    {
        return QByteArrayLiteral("[{\"Id\":\"sha256:aaaa\",\"RepoTags\":[\"alpine:latest\"],\"Size\":7700000,"
                                 "\"Created\":1789400000,\"Containers\":2}]");
    }

    /*! 按 Content-Length 或 chunked 写回响应（两种都要覆盖）。 */
    static void writeResponse(QLocalSocket *socket,
                              int status,
                              const QByteArray &body,
                              bool chunked,
                              const QByteArray &contentType = QByteArrayLiteral("application/json"))
    {
        QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + " OK\r\n";
        response += "Content-Type: " + contentType + "\r\n";
        if (chunked) {
            response += "Transfer-Encoding: chunked\r\n\r\n";
            response += QByteArray::number(body.size(), 16).toUpper() + "\r\n" + body + "\r\n0\r\n\r\n";
        } else {
            response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
        }
        socket->write(response);
        socket->flush();
        socket->disconnectFromServer();
    }

    QLocalServer m_server;
    QTemporaryDir m_directory;
    QString m_socketPath;
    QHash<QString, int> m_counts;
    QHash<QString, QPair<int, QByteArray>> m_overrides;
    QString m_apiVersion = QStringLiteral("1.56");
    bool m_failInfo = false;
};

class DockerBackendFakeEngineTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();

    void readsAllDataSetsThroughFakeEngine();
    void usesNegotiatedApiVersionPrefix();
    void coalescesConcurrentRefreshes();
    void clearsStaleCountsWhenInfoFailsOnLaterRefresh();
    void rejectsUnsupportedServerApiVersion();
    void mapsDaemonVersionRejectionToMismatchError();
    void readsStorageUsage();
    void readsContainerDetail();
    void readsContainerStats();
    void coalescesDuplicateDetailRequests();
    void stopsStatsSamplingOnRequest();

private:
    FakeEngine *m_engine = nullptr;
};

void DockerBackendFakeEngineTest::initTestCase()
{
    setupTranslationDomain();
    qRegisterMetaType<Kontainer::DockerError>("Kontainer::DockerError");
    m_engine = new FakeEngine(this);
    QVERIFY(m_engine->listen());
}

void DockerBackendFakeEngineTest::init()
{
    m_engine->reset();
}

void DockerBackendFakeEngineTest::readsAllDataSetsThroughFakeEngine()
{
    DockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(m_engine->socketPath()));

    QSignalSpy engineSpy(&backend, &DockerBackend::engineUpdated);
    QSignalSpy containersSpy(&backend, &DockerBackend::containersUpdated);
    QSignalSpy imagesSpy(&backend, &DockerBackend::imagesUpdated);
    QSignalSpy failureSpy(&backend, &DockerBackend::sectionFailed);

    backend.refreshAll();
    QTRY_VERIFY_WITH_TIMEOUT(engineSpy.count() == 1 && containersSpy.count() == 1 && imagesSpy.count() == 1, 10000);

    QCOMPARE(failureSpy.count(), 0);
    const EngineInfo info = backend.engineInfo();
    QVERIFY(info.available);
    QVERIFY(info.countsAvailable);
    QCOMPARE(info.serverVersion, QStringLiteral("99.0.0-fake"));
    QCOMPARE(info.apiVersion, QStringLiteral("1.56"));
    QCOMPARE(info.containerTotal, 2);
    QCOMPARE(info.containersRunning, 1);
    QCOMPARE(info.imageCount, 1);
    QCOMPARE(backend.containers().size(), 2);
    QCOMPARE(backend.containers().first().name, QStringLiteral("fake-running"));
    QCOMPARE(backend.containers().first().stateKey(), QStringLiteral("running"));
    QCOMPARE(backend.images().size(), 1);
    QVERIFY(!backend.isLoading());
}

void DockerBackendFakeEngineTest::usesNegotiatedApiVersionPrefix()
{
    DockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(m_engine->socketPath()));
    QSignalSpy engineSpy(&backend, &DockerBackend::engineUpdated);

    backend.refreshEngine();
    QTRY_COMPARE_WITH_TIMEOUT(engineSpy.count(), 1, 10000);

    // 协商前用无版本路径，协商后用 v1.56
    QVERIFY(m_engine->requestCount(QStringLiteral("/_ping")) >= 1);
    QVERIFY(m_engine->requestCount(QStringLiteral("/version")) >= 1);
    QVERIFY(m_engine->requestCount(QStringLiteral("/v1.56/info")) >= 1);
}

void DockerBackendFakeEngineTest::coalescesConcurrentRefreshes()
{
    DockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(m_engine->socketPath()));
    QSignalSpy containersSpy(&backend, &DockerBackend::containersUpdated);

    const int before = m_engine->requestCount(QStringLiteral("/v1.56/containers/json"));
    backend.refreshContainers();
    backend.refreshContainers();
    backend.refreshContainers();
    QTRY_COMPARE_WITH_TIMEOUT(containersSpy.count(), 1, 10000);

    // 三次刷新只允许产生一个请求（§17 Coalesce）
    QCOMPARE(m_engine->requestCount(QStringLiteral("/v1.56/containers/json")) - before, 1);
}

void DockerBackendFakeEngineTest::clearsStaleCountsWhenInfoFailsOnLaterRefresh()
{
    DockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(m_engine->socketPath()));

    QSignalSpy engineSpy(&backend, &DockerBackend::engineUpdated);
    backend.refreshAll();
    QTRY_VERIFY_WITH_TIMEOUT(engineSpy.count() >= 1, 10000);
    QVERIFY(backend.engineInfo().countsAvailable);
    QCOMPARE(backend.engineInfo().containerTotal, 2);

    // 第二次刷新：/info 失败，但 /version 仍然成功
    m_engine->setFailInfo(true);
    QSignalSpy secondEngineSpy(&backend, &DockerBackend::engineUpdated);
    QSignalSpy failureSpy(&backend, &DockerBackend::sectionFailed);

    backend.refreshAll();
    QTRY_VERIFY_WITH_TIMEOUT(secondEngineSpy.count() >= 1 && failureSpy.count() >= 1, 10000);

    const EngineInfo info = backend.engineInfo();
    QVERIFY(info.available); // 仍然连得上
    QVERIFY(!info.countsAvailable); // 但汇总计数必须作废
    QCOMPARE(info.containerTotal, 0);
    QCOMPARE(info.containersRunning, 0);
    QCOMPARE(info.containersStopped, 0);
    QCOMPARE(info.imageCount, 0);

}

void DockerBackendFakeEngineTest::rejectsUnsupportedServerApiVersion()
{
    m_engine->setApiVersion(QStringLiteral("1.20"));

    DockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(m_engine->socketPath()));
    QSignalSpy failureSpy(&backend, &DockerBackend::sectionFailed);

    backend.refreshEngine();
    QTRY_COMPARE_WITH_TIMEOUT(failureSpy.count(), 1, 10000);

    const auto error = failureSpy.first().at(1).value<DockerError>();
    QCOMPARE(int(error.kind()), int(DockerError::Kind::ApiVersionMismatch));
    QVERIFY(!backend.engineInfo().available);
}

void DockerBackendFakeEngineTest::mapsDaemonVersionRejectionToMismatchError()
{
    // 伪造 daemon 对"客户端版本过新"的 400 响应
    m_engine->setPathStatus(QStringLiteral("/info"),
                            400,
                            QByteArrayLiteral("{\"message\":\"client version 1.56 is too new. Maximum supported API version is 1.40\"}"));

    DockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(m_engine->socketPath()));
    QSignalSpy failureSpy(&backend, &DockerBackend::sectionFailed);

    backend.refreshEngine();
    QTRY_COMPARE_WITH_TIMEOUT(failureSpy.count(), 1, 10000);

    const auto error = failureSpy.first().at(1).value<DockerError>();
    QCOMPARE(int(error.kind()), int(DockerError::Kind::ApiVersionMismatch));
}

/*!
 * §23/§24：disk usage 来自结构化 API（/system/df），而不是 CLI。
 */
void DockerBackendFakeEngineTest::readsStorageUsage()
{
    DockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(m_engine->socketPath()));
    QSignalSpy storageSpy(&backend, &DockerBackend::storageUpdated);
    QSignalSpy failureSpy(&backend, &DockerBackend::sectionFailed);

    backend.refreshStorageUsage();
    QTRY_COMPARE_WITH_TIMEOUT(storageSpy.count(), 1, 10000);
    QCOMPARE(failureSpy.count(), 0);

    const StorageUsage usage = backend.storageUsage();
    QVERIFY(usage.valid);
    QCOMPARE(usage.imagesBytes, Q_INT64_C(49400000000));
    QCOMPARE(usage.containersBytes, Q_INT64_C(20340000));
    QCOMPARE(usage.volumesBytes, Q_INT64_C(0));
    QVERIFY(usage.buildCacheAvailable);
    QCOMPARE(usage.imageCount, 1);
    QCOMPARE(usage.totalBytes(), Q_INT64_C(49420340000));
}

/*!
 * §7/§26：容器详情来自 inspect，并经 DTO → domain 转换。
 */
void DockerBackendFakeEngineTest::readsContainerDetail()
{
    DockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(m_engine->socketPath()));
    QSignalSpy detailSpy(&backend, &DockerBackend::containerDetailUpdated);
    QSignalSpy failureSpy(&backend, &DockerBackend::sectionFailed);

    backend.inspectContainer(QStringLiteral("1111111111111111111111111111111111111111111111111111111111111111"));
    QTRY_COMPARE_WITH_TIMEOUT(detailSpy.count(), 1, 10000);
    QCOMPARE(failureSpy.count(), 0);

    const ContainerDetail detail = backend.containerDetail();
    QCOMPARE(detail.name, QStringLiteral("fake-running"));
    QCOMPARE(detail.image, QStringLiteral("alpine:latest")); // 来自 Config.Image，而不是镜像 ID
    QVERIFY(detail.imageId.startsWith(QLatin1String("sha256:aaaa")));
    QCOMPARE(int(detail.state), int(ContainerState::Running));
    QCOMPARE(int(detail.health), int(HealthState::Healthy));
    QCOMPARE(detail.status, QStringLiteral("running"));
    QCOMPARE(detail.pid, 4242);
    QCOMPARE(detail.restartCount, 1);
    QCOMPARE(detail.platform, QStringLiteral("linux"));
    QCOMPARE(detail.restartPolicy, QStringLiteral("unless-stopped"));
    QVERIFY(detail.created.isValid());
    QVERIFY(detail.started.isValid());
    // Go 的零值时间（从未发生）必须被解析为无效时间
    QVERIFY(!detail.finished.isValid());
    QCOMPARE(detail.environment.size(), 2);
    QCOMPARE(detail.command, (QStringList {QStringLiteral("sleep"), QStringLiteral("infinity")}));
    QCOMPARE(detail.entrypoint, (QStringList {QStringLiteral("/entry.sh")}));
    QCOMPARE(detail.workingDirectory, QStringLiteral("/work"));
    QCOMPARE(detail.hostname, QStringLiteral("fakehost"));
    QCOMPARE(detail.user, QStringLiteral("root"));
    QCOMPARE(detail.labels.size(), 1);
    QCOMPARE(detail.labels.first().first, QStringLiteral("com.example.role"));

    QCOMPARE(detail.ports.size(), 1);
    QCOMPARE(detail.ports.first().privatePort, quint16(8080));
    QCOMPARE(detail.ports.first().publicPort, quint16(18080));
    QCOMPARE(detail.ports.first().type, QStringLiteral("tcp"));

    QCOMPARE(detail.networks.size(), 1);
    QCOMPARE(detail.networks.first().name, QStringLiteral("bridge"));
    QCOMPARE(detail.networks.first().ipAddress, QStringLiteral("172.17.0.5"));
    QCOMPARE(detail.networks.first().macAddress, QStringLiteral("02:42:ac:11:00:05"));

    QCOMPARE(detail.mounts.size(), 1);
    QCOMPARE(detail.mounts.first().type, QStringLiteral("bind"));
    QCOMPARE(detail.mounts.first().source, QStringLiteral("/host/data"));
    QCOMPARE(detail.mounts.first().destination, QStringLiteral("/data"));
    QVERIFY(!detail.mounts.first().readOnly);
}

void DockerBackendFakeEngineTest::readsContainerStats()
{
    DockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(m_engine->socketPath()));
    QSignalSpy statsSpy(&backend, &DockerBackend::containerStatsUpdated);

    const QString id = QStringLiteral("1111111111111111111111111111111111111111111111111111111111111111");
    backend.requestContainerStats(id);
    QVERIFY(backend.isSamplingStats(id));
    QTRY_COMPARE_WITH_TIMEOUT(statsSpy.count(), 1, 10000);

    const ContainerStats stats = backend.containerStats();
    QCOMPARE(stats.containerId, id);
    QCOMPARE(stats.onlineCpus, 4);
    QCOMPARE(stats.memoryUsageBytes, quint64(110000000));
    QCOMPARE(stats.memoryCacheBytes, quint64(10000000));
    QCOMPARE(stats.memoryUsedBytes(), quint64(100000000));
    QCOMPARE(stats.networkRxBytes, quint64(1000));
    QCOMPARE(stats.networkTxBytes, quint64(2000));
    QCOMPARE(stats.blockReadBytes, quint64(4096));
    QCOMPARE(stats.blockWriteBytes, quint64(8192));
    QCOMPARE(stats.pids, 3);

    // (cpuDelta / systemDelta) × onlineCpus × 100 = (1e9 / 1e11) × 4 × 100 = 4%
    QVERIFY(qAbs(stats.cpuPercent() - 4.0) < 0.001);
}

/*!
 * §29：同一资源的 inspect 请求在途时必须合并，不能产生重复请求。
 */
void DockerBackendFakeEngineTest::coalescesDuplicateDetailRequests()
{
    DockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(m_engine->socketPath()));
    QSignalSpy detailSpy(&backend, &DockerBackend::containerDetailUpdated);

    const QString id = QStringLiteral("1111111111111111111111111111111111111111111111111111111111111111");
    const int before = m_engine->requestCount(QStringLiteral("/v1.56/containers/%1/json").arg(id));
    backend.inspectContainer(id);
    backend.inspectContainer(id);
    backend.inspectContainer(id);
    QTRY_COMPARE_WITH_TIMEOUT(detailSpy.count(), 1, 10000);

    QCOMPARE(m_engine->requestCount(QStringLiteral("/v1.56/containers/%1/json").arg(id)) - before, 1);
}

/*!
 * §27：离开详情页后必须停止 stats 采样（只结束本地请求生命周期）。
 */
void DockerBackendFakeEngineTest::stopsStatsSamplingOnRequest()
{
    DockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(m_engine->socketPath()));
    const QString id = QStringLiteral("1111111111111111111111111111111111111111111111111111111111111111");

    QSignalSpy statsSpy(&backend, &DockerBackend::containerStatsUpdated);
    backend.requestContainerStats(id);
    QTRY_COMPARE_WITH_TIMEOUT(statsSpy.count(), 1, 10000);
    QVERIFY(backend.isSamplingStats(id));

    backend.stopContainerStats(id);
    QVERIFY(!backend.isSamplingStats(id));

    // 停止后即使再次请求采样，也需要显式 start 才会继续（这里只验证状态语义）
    backend.stopContainerStats(id);
    QVERIFY(!backend.isSamplingStats(id));
}

QTEST_GUILESS_MAIN(DockerBackendFakeEngineTest)

#include "tst_docker_backend_against_fake_engine.moc"
