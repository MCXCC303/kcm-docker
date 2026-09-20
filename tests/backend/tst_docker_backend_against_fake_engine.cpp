/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/docker_backend.h"
#include "i18n.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QSet>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

using namespace Kontainer;

/*!
 * Drives the real DockerBackend against an in-process fake Docker Engine (Unix socket + minimal HTTP).
 *
 * Covers behavior hard to reproduce on a real daemon:
 *  - request coalescing (§17: consecutive refreshes produce one request)
 *  - stale counts must be dropped when a later /info refresh fails
 *  - API version negotiation failure / daemon 400 "too new"
 *  - both Content-Length and chunked responses
 *
 * Since phase 4 the fake Engine also serves write endpoints (start / stop / restart / remove /
 * image pull / image remove) and records each request's method, path and query: the mutation
 * contract tests (verb, parameters, status mapping) need no real daemon and never touch the
 * user's containers (ARCH_V4 §5.3).
 */
class FakeEngine : public QObject
{
    Q_OBJECT

public:
    struct RequestRecord {
        QString method;
        QString path;
        QString query;
        /*! Raw header block (`\r\n`-separated, no request line): auth tests assert `X-Registry-Auth`. */
        QByteArray headers;
        /*! Request body (network-create tests assert the JSON sent to the daemon). */
        QByteArray body;
    };

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

    /* --- Mutation observation and injection control --- */

    QList<RequestRecord> requests() const
    {
        return m_requests;
    }
    /*! Last request (for asserting verb and query). */
    RequestRecord lastRequest() const
    {
        return m_requests.isEmpty() ? RequestRecord{} : m_requests.last();
    }
    int requestCount(const QString &barePath, const QString &method = QString()) const
    {
        int count = 0;
        for (const RequestRecord &record : m_requests) {
            if (withoutVersionPrefix(record.path) == barePath && (method.isEmpty() || record.method == method)) {
                ++count;
            }
        }
        return count;
    }
    /*! Make one write endpoint return a given status and body (e.g. 409 / 304). */
    void setMutationResponse(const QString &barePath, int status, const QByteArray &body)
    {
        m_mutationResponses.insert(barePath, {status, body});
    }
    /*! Make a start / stop endpoint return 304 (engine's "already in target state" semantics). */
    void setMutationNotModified(const QString &barePath)
    {
        m_notModifiedMutations.insert(barePath);
    }
    /*! Pull-stream lines (one chunk each; covers lines split across chunks and many lines per chunk). */
    void setPullLines(const QList<QByteArray> &lines)
    {
        m_pullLines = lines;
    }
    /*! Pull-stream write delay: > 0 leaves event-loop time between chunks (used by cancel tests). */
    void setPullChunkDelayMs(int delayMs)
    {
        m_pullChunkDelayMs = delayMs;
    }

    /*!
     * Keep the connection open (simulates a silent `follow=1` stream).
     *
     * A real follow stream hangs while the container produces no output: only an explicit cancel
     * ends it, and we must **not** add an idle timeout (long-running task logs would look failed).
     */
    void setHoldConnection(bool hold)
    {
        m_holdConnection = hold;
    }

    void setFailInfo(bool fail)
    {
        m_failInfo = fail;
    }
    void setApiVersion(const QString &version)
    {
        m_apiVersion = version;
    }
    /*! Make one bare path return a given status (used to fake a 400 version error). */
    void setPathStatus(const QString &barePath, int status, const QByteArray &body)
    {
        m_overrides.insert(barePath, {status, body});
    }

    /*! Clear injected failures/overrides and counters between tests to avoid state leaks. */
    void reset()
    {
        m_holdConnection = false;
        m_overrides.clear();
        m_counts.clear();
        m_apiVersion = QStringLiteral("1.56");
        m_failInfo = false;
        m_requests.clear();
        m_mutationResponses.clear();
        m_notModifiedMutations.clear();
        m_pullLines = defaultPullLines();
        m_pullChunkDelayMs = 0;
    }

    /*! Default image pull stream: one progress step per layer + one completion line. */
    static QList<QByteArray> defaultPullLines()
    {
        return {
            QByteArrayLiteral("{\"status\":\"Pulling from library/alpine\",\"id\":\"3.19\"}\n"),
            QByteArrayLiteral("{\"status\":\"Downloading\",\"progressDetail\":{\"current\":50,\"total\":100},\"id\":\"aaa\"}\n"),
            QByteArrayLiteral("{\"status\":\"Downloading\",\"progressDetail\":{\"current\":100,\"total\":100},\"id\":\"aaa\"}\n"
                              "{\"status\":\"Pull complete\",\"id\":\"aaa\"}\n"),
            QByteArrayLiteral("{\"status\":\"Downloading\",\"progressDetail\":{\"current\":10,\"total\":40},\"id\":\"bbb\"}\n"),
            QByteArrayLiteral("{\"status\":\"Pull complete\",\"id\":\"bbb\"}\n"),
            QByteArrayLiteral("{\"status\":\"Status: Downloaded newer image for alpine:3.19\"}\n"),
        };
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
                const QString method = QString::fromLatin1(parts.at(0));
                const QByteArray headers = buffer->mid(requestLine.size() + 2, headerEnd - requestLine.size() - 2);
                respond(socket, method, QString::fromLatin1(parts.at(1)), headers, buffer->mid(headerEnd + 4));
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

    void respond(QLocalSocket *socket,
                 const QString &method,
                 const QString &rawTarget,
                 const QByteArray &headers = {},
                 const QByteArray &body = {})
    {
        const QString path = rawTarget.section(QLatin1Char('?'), 0, 0);
        const QString query = rawTarget.section(QLatin1Char('?'), 1, 1);
        m_counts[path] += 1;
        m_requests.append({method, path, query, headers, body});
        const QString bare = withoutVersionPrefix(path);

        if (const auto override = m_overrides.constFind(bare); override != m_overrides.constEnd()) {
            if (m_holdConnection) {
                // Headers + chunked declaration only, no terminal chunk: the client waits until cancelled
                QByteArray head = "HTTP/1.1 " + QByteArray::number(override->first) + " OK\r\n";
                head += "Content-Type: application/vnd.docker.raw-stream\r\n";
                head += "Transfer-Encoding: chunked\r\n\r\n";
                head += override->second; // allow some backlog data first
                socket->write(head);
                socket->flush();
                return;
            }
            writeResponse(socket, override->first, override->second, false);
            return;
        }

        /* --- Write endpoints (ARCH_V4 §2.2.4): check injected responses first --- */
        if (const auto injected = m_mutationResponses.constFind(bare); injected != m_mutationResponses.constEnd()) {
            writeResponse(socket, injected->first, injected->second, false);
            return;
        }
        if (method == QLatin1String("POST") && bare == QLatin1String("/images/create")) {
            writePullStream(socket, m_pullLines, m_pullChunkDelayMs);
            return;
        }
        if (method == QLatin1String("POST") && bare.startsWith(QLatin1String("/containers/"))) {
            // every write other than start (already running) / stop (already stopped) returns 204
            const bool isStart = bare.endsWith(QLatin1String("/start"));
            const bool isStop = bare.endsWith(QLatin1String("/stop"));
            if ((isStart || isStop) && m_notModifiedMutations.contains(bare)) {
                writeResponse(socket, 304, QByteArray(), false);
                return;
            }
            writeResponse(socket, 204, QByteArray(), false);
            return;
        }
        if (method == QLatin1String("DELETE") && bare.startsWith(QLatin1String("/containers/"))) {
            writeResponse(socket, 204, QByteArray(), false);
            return;
        }
        if (method == QLatin1String("DELETE") && bare.startsWith(QLatin1String("/images/"))) {
            writeResponse(socket, 200, QByteArrayLiteral("[{\"Deleted\":\"sha256:aaaa\"}]"), false);
            return;
        }
        if (method != QLatin1String("GET")) {
            writeResponse(socket, 405, QByteArrayLiteral("{\"message\":\"fake engine: method not allowed\"}"), false);
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
                                 "\"WorkingDir\":\"/work\",\"Hostname\":\"fakehost\",\"User\":\"root\",\"Tty\":false,"
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
        // Second sample: clear delta vs. precpu so the CPU math is checkable
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

    /*! Write the response as Content-Length or chunked (both must be covered). */
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

    /*!
     * Write the pull stream chunk by chunk: real daemon chunk boundaries ignore JSON line boundaries,
     * so each line gets its own chunk, with optional delays between chunks (cancel tests).
     */
    static void writePullStream(QLocalSocket *socket, const QList<QByteArray> &lines, int delayMs)
    {
        QByteArray head = QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nTransfer-Encoding: chunked\r\n\r\n");
        socket->write(head);
        socket->flush();

        int index = 0;
        for (const QByteArray &line : lines) {
            const QByteArray chunk = QByteArray::number(line.size(), 16).toUpper() + "\r\n" + line + "\r\n";
            if (delayMs <= 0) {
                socket->write(chunk);
                socket->flush();
            } else {
                QTimer::singleShot(delayMs * (index + 1), socket, [socket, chunk] {
                    socket->write(chunk);
                    socket->flush();
                });
            }
            ++index;
        }

        const int finishDelay = delayMs <= 0 ? 0 : delayMs * (lines.size() + 1);
        QTimer::singleShot(finishDelay, socket, [socket] {
            socket->write(QByteArrayLiteral("0\r\n\r\n"));
            socket->flush();
            socket->disconnectFromServer();
        });
    }

    QLocalServer m_server;
    QTemporaryDir m_directory;
    QString m_socketPath;
    QHash<QString, int> m_counts;
    QHash<QString, QPair<int, QByteArray>> m_overrides;
    QString m_apiVersion = QStringLiteral("1.56");
    bool m_failInfo = false;
    bool m_holdConnection = false;

    QList<RequestRecord> m_requests;
    QHash<QString, QPair<int, QByteArray>> m_mutationResponses;
    QSet<QString> m_notModifiedMutations;
    QList<QByteArray> m_pullLines = defaultPullLines();
    int m_pullChunkDelayMs = 0;
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

    /* --- Mutation contracts (ARCH_V4 §5.1) --- */
    void startContainerSendsPostWithoutParameters();
    void stopContainerSendsTimeoutParameter();
    void removeContainerUsesDeleteWithoutVolumeFlag();
    void notModifiedStartIsReportedAsUnchanged();
    void containerMutationFailureKeepsEngineMessage();
    void pullImageSendsFromImageAndTag();
    void pullImageAggregatesLayerProgress();
    void pullImageErrorLineFailsTheMutation();
    void pullImageCanBeCancelled();
    void removeImageSendsForceFlag();
    void mutationWaitsForApiVersionHandshake();
    void concurrentPullsAreIndependent();
    void authCheckSendsCredentialsInTheRequestBody();
    void authCheckClassifiesFailures();
    void pullSendsCredentialsOnlyWhenPresent();
    void networksAreListedFromTheEngine();
    void networkMembersComeFromTheContainerList();
    void networkCreateSendsJsonBodyAndRemoveUsesDelete();
    void volumesAreListedFromTheEngine();
    void createsAContainerWithNameInTheQuery();
    void networkConnectAndDisconnectSendTheContainer();
    void logStreamDemultiplexesAndEnds();
    void logStreamReportsEngineFailures();
    void logStreamCancelIsNotAnError();
    void logStreamReconnectDropsTheOldStream();

private:
    FakeEngine *m_engine = nullptr;
};

/* ============================================================================
 * Registry credential check (ARCH_V5_V8 §2.6)
 * ==========================================================================*/

namespace
{
using AuthResult = DockerBackendInterface::AuthCheckResult;

RegistryCredential sampleCredential()
{
    RegistryCredential credential;
    credential.serverAddress = QStringLiteral("https://index.docker.io/v1/");
    credential.username = QStringLiteral("alice");
    credential.password = QStringLiteral("s3cret");
    return credential;
}
} // namespace

/*!
 * Credentials go in the **request body** (like the docker CLI), never in the URL.
 *
 * Measured lesson: an empty body with only the `X-Registry-Auth` header makes the engine answer
 * `400 invalid X-Registry-Auth header: invalid JSON: EOF` - so every registry check failed.
 * The fake engine records the body verbatim for assertions.
 */
/*!
 * Private registry pull: credentials travel only in the `X-Registry-Auth` header, and `serveraddress`
 * must be the **registry holding the image** (the caller's credential may name a different one).
 */
/*!
 * Network list (ARCH_V5_V8 §3.2): request path, parsing and signals.
 */
void DockerBackendFakeEngineTest::networksAreListedFromTheEngine()
{
    const QByteArray payload = R"([
        {"Name": "bridge", "Id": "5cd9ee041dffb38f712fa8b9284ef54c3fcaffd720958515aa3c155b82af90b9",
         "Driver": "bridge", "Scope": "local",
         "IPAM": {"Config": [{"Subnet": "172.17.0.0/16", "Gateway": "172.17.0.1"}]},
         "Options": {"com.docker.network.bridge.name": "docker0"}, "Labels": {},
         "Containers": {}},
        {"Name": "app_default", "Id": "aaaaaaaaaaaabbbbbbbbbbccccccccccddddddddddeeeeeeeeeeffffffffff1234",
         "Driver": "bridge", "Scope": "local",
         "IPAM": {"Config": [{"Subnet": "172.18.0.0/16"}]},
         "Labels": {"com.docker.compose.project": "app"},
         "Containers": {"1111111111111111111111111111111111111111111111111111111111111111":
                        {"Name": "app", "MacAddress": "02:42:ac:12:00:02", "IPv4Address": "172.18.0.2/16"}}}
    ])";
    m_engine->setPathStatus(QStringLiteral("/networks"), 200, payload);

    DockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(m_engine->socketPath()));
    QSignalSpy updatedSpy(&backend, &DockerBackend::networksUpdated);
    QSignalSpy failureSpy(&backend, &DockerBackend::sectionFailed);

    backend.refreshNetworks();
    QTRY_COMPARE_WITH_TIMEOUT(updatedSpy.count(), 1, 10000);

    QCOMPARE(failureSpy.count(), 0);
    const QList<Network> networks = backend.networks();
    QCOMPARE(networks.size(), 2);
    QVERIFY(networks.at(0).isPredefined());
    QVERIFY(!networks.at(1).isPredefined());
    QCOMPARE(networks.at(1).memberCount(), 1);
    QCOMPARE(networks.at(1).subnetText(), QStringLiteral("172.18.0.0/16"));

    // Request shape: version prefix + plain GET
    const FakeEngine::RequestRecord request = m_engine->lastRequest();
    QCOMPARE(request.method, QStringLiteral("GET"));
    QCOMPARE(request.path, QStringLiteral("/v1.56/networks"));
    QVERIFY(request.query.isEmpty());
}


/* ============================================================================
 * Container logs (ARCH_V5_V8 §3.1)
 * ==========================================================================*/

namespace
{
using LogEnd = DockerBackendInterface::LogStreamEnd;

QStringList logTexts(const QVariantList &lines)
{
    QStringList texts;
    for (const QVariant &value : lines) {
        texts.append(value.value<Kontainer::LogLine>().text);
    }
    return texts;
}
} // namespace

/*!
 * History + follow: request shape (stdout/stderr/follow/tail) and frame demultiplexing must both be right.
 * The fake engine answers with a stdcopy frame stream, like container logs.
 */
void DockerBackendFakeEngineTest::logStreamDemultiplexesAndEnds()
{
    DockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(m_engine->socketPath()));

    // Server sends two frames at once: one stdout line, one stderr line
    QByteArray body;
    body += QByteArray("\x01\x00\x00\x00\x00\x00\x00\x06hello\n", 14);
    body += QByteArray("\x02\x00\x00\x00\x00\x00\x00\x07warning", 15);
    m_engine->setPathStatus(QStringLiteral("/containers/logs-container/logs"), 200, body);

    QSignalSpy linesSpy(&backend, &DockerBackend::containerLogLines);
    QSignalSpy finishedSpy(&backend, &DockerBackend::containerLogsFinished);

    backend.startContainerLogs(QStringLiteral("logs-container"), false, true, 200);
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);

    const FakeEngine::RequestRecord request = m_engine->lastRequest();
    QCOMPARE(request.method, QStringLiteral("GET"));
    QCOMPARE(request.path, QStringLiteral("/v1.56/containers/logs-container/logs"));
    QVERIFY(request.query.contains(QStringLiteral("stdout=1")));
    QVERIFY(request.query.contains(QStringLiteral("stderr=1")));
    QVERIFY(request.query.contains(QStringLiteral("follow=1")));
    QVERIFY(request.query.contains(QStringLiteral("tail=200")));

    QVERIFY(linesSpy.count() >= 1);
    QStringList texts;
    for (const QVariantList &call : linesSpy) {
        texts += logTexts(call.at(1).toList());
    }
    QCOMPARE(texts, QStringList({QStringLiteral("hello"), QStringLiteral("warning")}));

    // Stream ends naturally (container stopped / history consumed): Ended, not an error
    QCOMPARE(finishedSpy.at(0).at(1).value<LogEnd>(), LogEnd::Ended);
}

void DockerBackendFakeEngineTest::logStreamReportsEngineFailures()
{
    DockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(m_engine->socketPath()));
    m_engine->setPathStatus(QStringLiteral("/containers/gone/logs"), 404, QByteArrayLiteral("{\"message\":\"No such container\"}"));

    QSignalSpy finishedSpy(&backend, &DockerBackend::containerLogsFinished);
    backend.startContainerLogs(QStringLiteral("gone"), false, true, 100);
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);

    QCOMPARE(finishedSpy.at(0).at(1).value<LogEnd>(), LogEnd::Failed);
    const DockerError error = finishedSpy.at(0).at(2).value<DockerError>();
    QCOMPARE(error.kind(), DockerError::Kind::NotFound);
}

void DockerBackendFakeEngineTest::logStreamCancelIsNotAnError()
{
    DockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(m_engine->socketPath()));
    // Follow stream: the server holds the connection open, so we cancel it
    m_engine->setPathStatus(QStringLiteral("/containers/tailing/logs"), 200, QByteArrayLiteral(""));
    m_engine->setHoldConnection(true);

    QSignalSpy finishedSpy(&backend, &DockerBackend::containerLogsFinished);
    backend.startContainerLogs(QStringLiteral("tailing"), false, true, 50);
    QTest::qWait(200); // wait until the request is really sent
    backend.stopContainerLogs(QStringLiteral("tailing"));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);

    QCOMPARE(finishedSpy.at(0).at(1).value<LogEnd>(), LogEnd::Cancelled);
    m_engine->setHoldConnection(false);

    // Idempotent: stopping again must not emit another signal
    backend.stopContainerLogs(QStringLiteral("tailing"));
    QTest::qWait(50);
    QCOMPARE(finishedSpy.count(), 1);
}

void DockerBackendFakeEngineTest::logStreamReconnectDropsTheOldStream()
{
    DockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(m_engine->socketPath()));
    m_engine->setPathStatus(QStringLiteral("/containers/reconnect/logs"), 200, QByteArrayLiteral(""));
    m_engine->setHoldConnection(true);

    QSignalSpy finishedSpy(&backend, &DockerBackend::containerLogsFinished);
    backend.startContainerLogs(QStringLiteral("reconnect"), false, true, 50);
    QTest::qWait(200);
    // Reconnect: stop the old stream, start a new one. The old stream's teardown must not clear the new state
    backend.startContainerLogs(QStringLiteral("reconnect"), true, false, 10);
    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() >= 1, 10000);
    QCOMPARE(finishedSpy.at(0).at(1).value<LogEnd>(), LogEnd::Cancelled);

    m_engine->setHoldConnection(false);
    backend.stopContainerLogs(QStringLiteral("reconnect"));
    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() >= 2, 10000);
}

void DockerBackendFakeEngineTest::pullSendsCredentialsOnlyWhenPresent()
{
    DockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(m_engine->socketPath()));
    m_engine->setPullLines({QByteArrayLiteral("{\"status\":\"Pull complete\"}\n")});

    // Anonymous pull: no header at all
    QSignalSpy finishedSpy(&backend, &DockerBackend::mutationFinished);
    backend.pullImage(QStringLiteral("alpine:3.19"));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);
    QVERIFY2(!m_engine->lastRequest().headers.contains("X-Registry-Auth"),
             "anonymous pulls must not send an auth header");

    // With credentials: the header decodes back to them, serveraddress rewritten to the image's registry
    RegistryCredential credential;
    credential.serverAddress = QStringLiteral("index.docker.io"); // deliberately a different registry
    credential.username = QStringLiteral("alice");
    credential.password = QStringLiteral("s3cret");
    backend.pullImage(QStringLiteral("registry.example.com:5000/team/app:1.0"), credential);
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 2, 10000);

    const FakeEngine::RequestRecord request = m_engine->lastRequest();
    QVERIFY2(!request.headers.contains("s3cret"), "the raw password must never be sent in a header");
    const int headerStart = request.headers.indexOf("X-Registry-Auth: ") + int(qstrlen("X-Registry-Auth: "));
    QVERIFY2(headerStart > int(qstrlen("X-Registry-Auth: ")) - 1, "the pull must carry the auth header");
    QString errorKey;
    const RegistryCredential decoded = RegistryAuth::decode(request.headers.mid(headerStart).split('\r').value(0), &errorKey);
    QVERIFY2(errorKey.isEmpty(), qPrintable(errorKey));
    QCOMPARE(decoded.username, QStringLiteral("alice"));
    QCOMPARE(decoded.serverAddress, QStringLiteral("registry.example.com:5000"));
}

void DockerBackendFakeEngineTest::authCheckSendsCredentialsInTheRequestBody()
{
    DockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(m_engine->socketPath()));
    m_engine->setPathStatus(QStringLiteral("/auth"), 200, QByteArrayLiteral("{\"Status\":\"Login Succeeded\"}"));

    QSignalSpy checkedSpy(&backend, &DockerBackend::registryAuthChecked);
    backend.checkRegistryAuth(QStringLiteral("registry.example.com"), sampleCredential());
    QTRY_COMPARE_WITH_TIMEOUT(checkedSpy.count(), 1, 10000);

    const FakeEngine::RequestRecord request = m_engine->lastRequest();
    QCOMPARE(request.method, QStringLiteral("POST"));
    QCOMPARE(request.path, QStringLiteral("/v1.56/auth"));
    QVERIFY2(request.query.isEmpty(), "credentials must never appear in the URL");
    QVERIFY2(!request.headers.contains("s3cret"), "the raw password must never be sent in a header");

    // Body is the JSON the docker CLI uses (keys match the engine)
    const QJsonObject payload = QJsonDocument::fromJson(request.body).object();
    QCOMPARE(payload.value(QStringLiteral("username")).toString(), QStringLiteral("alice"));
    QCOMPARE(payload.value(QStringLiteral("password")).toString(), QStringLiteral("s3cret"));
    // serveraddress uses the caller's registry, not the index.docker.io in the credential
    QCOMPARE(payload.value(QStringLiteral("serveraddress")).toString(), QStringLiteral("registry.example.com"));

    // Result: success, reporting the normalized registry address
    QCOMPARE(checkedSpy.at(0).at(0).toString(), QStringLiteral("registry.example.com"));
    QCOMPARE(checkedSpy.at(0).at(1).value<AuthResult>(), AuthResult::Succeeded);
}

void DockerBackendFakeEngineTest::authCheckClassifiesFailures()
{
    DockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(m_engine->socketPath()));
    QSignalSpy checkedSpy(&backend, &DockerBackend::registryAuthChecked);

    // 401: wrong username/password (the engine text may only say unauthorized)
    m_engine->setPathStatus(QStringLiteral("/auth"), 401,
                            QByteArrayLiteral("{\"message\":\"unauthorized: incorrect username or password\"}"));
    backend.checkRegistryAuth(QStringLiteral("registry.example.com"), sampleCredential());
    QTRY_COMPARE_WITH_TIMEOUT(checkedSpy.count(), 1, 10000);
    QCOMPARE(checkedSpy.at(0).at(1).value<AuthResult>(), AuthResult::InvalidCredentials);

    // 500 whose message is a network error -> "registry unreachable" (UI: check network/proxy, not password)
    m_engine->setPathStatus(QStringLiteral("/auth"), 500,
                            QByteArrayLiteral("{\"message\":\"dial tcp: lookup registry.invalid: no such host\"}"));
    backend.checkRegistryAuth(QStringLiteral("registry.example.com"), sampleCredential());
    QTRY_COMPARE_WITH_TIMEOUT(checkedSpy.count(), 2, 10000);
    QCOMPARE(checkedSpy.at(1).at(1).value<AuthResult>(), AuthResult::RegistryUnreachable);

    // 500 with timeout wording (measured: engine cannot reach registry) -> "registry unreachable"
    m_engine->setPathStatus(QStringLiteral("/auth"), 500,
                            QByteArrayLiteral("{\"message\":\"Get \\\"https://registry-1.docker.io/v2/\\\": context deadline exceeded\"}"));
    backend.checkRegistryAuth(QStringLiteral("registry.example.com"), sampleCredential());
    QTRY_COMPARE_WITH_TIMEOUT(checkedSpy.count(), 3, 10000);
    QCOMPARE(checkedSpy.at(2).at(1).value<AuthResult>(), AuthResult::RegistryUnreachable);

    // 500 with no network clue: plain failure (no guessing)
    m_engine->setPathStatus(QStringLiteral("/auth"), 500, QByteArrayLiteral("{\"message\":\"something else went wrong\"}"));
    backend.checkRegistryAuth(QStringLiteral("registry.example.com"), sampleCredential());
    QTRY_COMPARE_WITH_TIMEOUT(checkedSpy.count(), 4, 10000);
    QCOMPARE(checkedSpy.at(3).at(1).value<AuthResult>(), AuthResult::Failed);

    // Incomplete parameters: no request sent, reported as "bad credentials"
    const int requestsBefore = m_engine->requests().size();
    RegistryCredential incomplete;
    incomplete.serverAddress = QStringLiteral("registry.example.com");
    backend.checkRegistryAuth(QStringLiteral("registry.example.com"), incomplete);
    QTRY_COMPARE_WITH_TIMEOUT(checkedSpy.count(), 5, 10000);
    QCOMPARE(checkedSpy.at(4).at(1).value<AuthResult>(), AuthResult::InvalidCredentials);
    QCOMPARE(m_engine->requests().size(), requestsBefore);
}

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

    // Unversioned paths before negotiation, v1.56 after
    QVERIFY(m_engine->requestCount(QStringLiteral("/_ping")) >= 1);
    QVERIFY(m_engine->requestCount(QStringLiteral("/version")) >= 1);
    QVERIFY(m_engine->requestCount(QStringLiteral("/info")) >= 1);
}

void DockerBackendFakeEngineTest::coalescesConcurrentRefreshes()
{
    DockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(m_engine->socketPath()));
    QSignalSpy containersSpy(&backend, &DockerBackend::containersUpdated);

    const int before = m_engine->requestCount(QStringLiteral("/containers/json"));
    backend.refreshContainers();
    backend.refreshContainers();
    backend.refreshContainers();
    QTRY_COMPARE_WITH_TIMEOUT(containersSpy.count(), 1, 10000);

    // Three refreshes must produce exactly one request (§17 Coalesce)
    QCOMPARE(m_engine->requestCount(QStringLiteral("/containers/json")) - before, 1);
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

    // Second refresh: /info fails but /version still succeeds
    m_engine->setFailInfo(true);
    QSignalSpy secondEngineSpy(&backend, &DockerBackend::engineUpdated);
    QSignalSpy failureSpy(&backend, &DockerBackend::sectionFailed);

    backend.refreshAll();
    QTRY_VERIFY_WITH_TIMEOUT(secondEngineSpy.count() >= 1 && failureSpy.count() >= 1, 10000);

    const EngineInfo info = backend.engineInfo();
    QVERIFY(info.available); // still reachable
    QVERIFY(!info.countsAvailable); // but the summary counts must be dropped
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
    // Fake the daemon's 400 response for "client version too new"
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
 * §23/§24: disk usage comes from the structured API (/system/df), not from the CLI.
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
 * §7/§26: container detail comes from inspect and goes through the DTO → domain mapping.
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
    QCOMPARE(detail.image, QStringLiteral("alpine:latest")); // from Config.Image, not the image ID
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
    // Go's zero time (never happened) must parse as an invalid time
    QVERIFY(!detail.finished.isValid());
    QCOMPARE(detail.environment.size(), 2);
    QCOMPARE(detail.command, (QStringList {QStringLiteral("sleep"), QStringLiteral("infinity")}));
    QCOMPARE(detail.entrypoint, (QStringList {QStringLiteral("/entry.sh")}));
    QCOMPARE(detail.workingDirectory, QStringLiteral("/work"));
    QCOMPARE(detail.hostname, QStringLiteral("fakehost"));
    // Log stream branches on Config.Tty: getting it wrong treats the 8-byte frame header as log text
    QVERIFY2(!detail.tty, "the fixture is a non-TTY container");
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
 * §29: in-flight inspect requests for the same resource must coalesce, never duplicate.
 */
void DockerBackendFakeEngineTest::coalescesDuplicateDetailRequests()
{
    DockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(m_engine->socketPath()));
    QSignalSpy detailSpy(&backend, &DockerBackend::containerDetailUpdated);

    const QString id = QStringLiteral("1111111111111111111111111111111111111111111111111111111111111111");
    const int before = m_engine->requestCount(QStringLiteral("/containers/%1/json").arg(id));
    backend.inspectContainer(id);
    backend.inspectContainer(id);
    backend.inspectContainer(id);
    QTRY_COMPARE_WITH_TIMEOUT(detailSpy.count(), 1, 10000);

    QCOMPARE(m_engine->requestCount(QStringLiteral("/containers/%1/json").arg(id)) - before, 1);
}

/*!
 * §27: leaving the detail page must stop stats sampling (ends only the local request lifecycle).
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

    // After stopping, sampling needs an explicit start to resume (only state semantics checked here)
    backend.stopContainerStats(id);
    QVERIFY(!backend.isSamplingStats(id));
}

/* ============================================================================
 * Mutation contracts (ARCH_V4 §2.2.4 / §5.1)
 *
 * These tests assert which requests we actually send and how engine status codes are mapped,
 * all against the fake Engine: no real daemon, no container required.
 * ==========================================================================*/

namespace
{
const QString kContainerId = QStringLiteral("1111111111111111111111111111111111111111111111111111111111111111");

using Mutation = DockerBackendInterface::Mutation;
using Outcome = DockerBackendInterface::MutationOutcome;
} // namespace

void DockerBackendFakeEngineTest::startContainerSendsPostWithoutParameters()
{
    DockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(m_engine->socketPath()));

    QSignalSpy finishedSpy(&backend, &DockerBackend::mutationFinished);
    backend.startContainer(kContainerId);
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);

    const FakeEngine::RequestRecord request = m_engine->lastRequest();
    QCOMPARE(request.method, QStringLiteral("POST"));
    QCOMPARE(request.path, QStringLiteral("/v1.56/containers/%1/start").arg(kContainerId));
    QVERIFY(request.query.isEmpty());

    QCOMPARE(finishedSpy.at(0).at(0).value<Mutation>(), Mutation::StartContainer);
    QCOMPARE(finishedSpy.at(0).at(1).toString(), QStringLiteral("container:") + kContainerId);
    QCOMPARE(finishedSpy.at(0).at(2).value<Outcome>(), Outcome::Succeeded);
}

void DockerBackendFakeEngineTest::stopContainerSendsTimeoutParameter()
{
    DockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(m_engine->socketPath()));

    QSignalSpy finishedSpy(&backend, &DockerBackend::mutationFinished);
    backend.stopContainer(kContainerId);
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);

    const FakeEngine::RequestRecord request = m_engine->lastRequest();
    QCOMPARE(request.method, QStringLiteral("POST"));
    QCOMPARE(request.path, QStringLiteral("/v1.56/containers/%1/stop").arg(kContainerId));
    // The stop grace period is decided by the backend (RefreshPolicy), not passed by the UI
    QCOMPARE(request.query, QStringLiteral("t=%1").arg(RefreshPolicy::kStopTimeoutSeconds));
    QCOMPARE(finishedSpy.at(0).at(2).value<Outcome>(), Outcome::Succeeded);
}

void DockerBackendFakeEngineTest::removeContainerUsesDeleteWithoutVolumeFlag()
{
    DockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(m_engine->socketPath()));

    QSignalSpy finishedSpy(&backend, &DockerBackend::mutationFinished);
    backend.removeContainer(kContainerId);
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);

    const FakeEngine::RequestRecord request = m_engine->lastRequest();
    QCOMPARE(request.method, QStringLiteral("DELETE"));
    QCOMPARE(request.path, QStringLiteral("/v1.56/containers/%1").arg(kContainerId));
    // No volume removal (no v), no force: deliberate data protection
    QVERIFY(request.query.isEmpty());
    QCOMPARE(finishedSpy.at(0).at(2).value<Outcome>(), Outcome::Succeeded);
    QVERIFY(!finishedSpy.at(0).at(3).value<DockerError>().isError());
}

void DockerBackendFakeEngineTest::notModifiedStartIsReportedAsUnchanged()
{
    m_engine->setMutationNotModified(QStringLiteral("/containers/%1/start").arg(kContainerId));

    DockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(m_engine->socketPath()));

    QSignalSpy finishedSpy(&backend, &DockerBackend::mutationFinished);
    backend.startContainer(kContainerId);
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);

    // 304 means "already running", not an error
    QCOMPARE(finishedSpy.at(0).at(2).value<Outcome>(), Outcome::Unchanged);
    QVERIFY(!finishedSpy.at(0).at(3).value<DockerError>().isError());
}

void DockerBackendFakeEngineTest::containerMutationFailureKeepsEngineMessage()
{
    m_engine->setMutationResponse(QStringLiteral("/containers/%1").arg(kContainerId),
                                  409,
                                  QByteArrayLiteral("{\"message\":\"You cannot remove a running container. Stop the container before attempting removal or force remove\"}"));

    DockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(m_engine->socketPath()));

    QSignalSpy finishedSpy(&backend, &DockerBackend::mutationFinished);
    backend.removeContainer(kContainerId);
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);

    const DockerError error = finishedSpy.at(0).at(3).value<DockerError>();
    QCOMPARE(finishedSpy.at(0).at(2).value<Outcome>(), Outcome::Failed);
    QCOMPARE(error.kind(), DockerError::Kind::Conflict);
    QCOMPARE(error.httpStatus(), 409);
    // Keep the engine message: the text mapping tells running / in use / name conflict apart
    QVERIFY(error.detail().contains(QStringLiteral("running container")));
}

void DockerBackendFakeEngineTest::pullImageSendsFromImageAndTag()
{
    DockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(m_engine->socketPath()));

    QSignalSpy finishedSpy(&backend, &DockerBackend::mutationFinished);
    // No tag -> normalized to latest, but the tag parameter is still sent explicitly
    backend.pullImage(QStringLiteral("alpine"));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);

    const FakeEngine::RequestRecord request = m_engine->lastRequest();
    QCOMPARE(request.method, QStringLiteral("POST"));
    QCOMPARE(request.path, QStringLiteral("/v1.56/images/create"));
    QVERIFY(request.query.contains(QStringLiteral("fromImage=alpine")));
    QVERIFY(request.query.contains(QStringLiteral("tag=latest")));
    QCOMPARE(finishedSpy.at(0).at(2).value<Outcome>(), Outcome::Succeeded);
}

void DockerBackendFakeEngineTest::pullImageAggregatesLayerProgress()
{
    DockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(m_engine->socketPath()));

    QList<ImagePullProgress> progress;
    connect(&backend, &DockerBackend::imagePullProgress, this, [&progress](const ImagePullProgress &p) {
        progress.append(p);
    });

    QSignalSpy finishedSpy(&backend, &DockerBackend::mutationFinished);
    backend.pullImage(QStringLiteral("alpine:3.19"));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);

    QVERIFY(progress.size() >= 4);
    // Two layers, 140 bytes total; when the last layer completes the total is known and full
    const ImagePullProgress last = progress.last();
    QCOMPARE(last.reference, QStringLiteral("alpine:3.19"));
    QCOMPARE(last.totalLayers, 2);
    QCOMPARE(last.completedLayers, 2);
    QCOMPARE(last.totalBytes, qint64(140));
    QCOMPARE(last.currentBytes, qint64(140));
    QCOMPARE(last.fraction(), 1.0);
    QVERIFY(last.statusText.contains(QStringLiteral("Downloaded newer image")));
    QVERIFY(!last.failed());

    // At least one intermediate update is indeterminate (unknown total): the progress bar must handle it
    bool sawIndeterminate = false;
    for (const ImagePullProgress &p : progress) {
        if (p.isIndeterminate() && p.phase != ImagePullProgress::Phase::Complete) {
            sawIndeterminate = true;
        }
    }
    QVERIFY(sawIndeterminate);

    // Progress must be monotonic (the UI progress bar must not go backwards)
    qint64 previous = 0;
    for (const ImagePullProgress &p : progress) {
        QVERIFY(p.currentBytes >= previous);
        previous = p.currentBytes;
    }
}

void DockerBackendFakeEngineTest::pullImageErrorLineFailsTheMutation()
{
    // HTTP is 200, the failure is inside the stream: a common Docker shape, must not count as success
    m_engine->setPullLines({
        QByteArrayLiteral("{\"status\":\"Pulling from library/nope\"}\n"),
        QByteArrayLiteral("{\"errorDetail\":{\"message\":\"manifest unknown\"},\"error\":\"manifest unknown\"}\n"),
    });

    DockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(m_engine->socketPath()));

    QSignalSpy finishedSpy(&backend, &DockerBackend::mutationFinished);
    backend.pullImage(QStringLiteral("nope/nope:none"));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);

    QCOMPARE(finishedSpy.at(0).at(2).value<Outcome>(), Outcome::Failed);
    QCOMPARE(finishedSpy.at(0).at(3).value<DockerError>().detail(), QStringLiteral("manifest unknown"));
}

void DockerBackendFakeEngineTest::pullImageCanBeCancelled()
{
    m_engine->setPullChunkDelayMs(40);

    DockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(m_engine->socketPath()));

    // Cancel when the first progress frame arrives: no timing guesses, no dependence on handshake latency
    bool requestedCancel = false;
    connect(&backend, &DockerBackend::imagePullProgress, this, [&backend, &requestedCancel] {
        if (!requestedCancel) {
            requestedCancel = true;
            backend.cancelImagePull(QStringLiteral("alpine:3.19"));
        }
    });

    QSignalSpy finishedSpy(&backend, &DockerBackend::mutationFinished);
    backend.pullImage(QStringLiteral("alpine:3.19"));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);

    // Cancelling is not an error, but the outcome must be reported exactly once
    QCOMPARE(finishedSpy.at(0).at(2).value<Outcome>(), Outcome::Cancelled);
    QVERIFY(!finishedSpy.at(0).at(3).value<DockerError>().isError());
    QTest::qWait(300);
    QCOMPARE(finishedSpy.count(), 1);
}

void DockerBackendFakeEngineTest::removeImageSendsForceFlag()
{
    DockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(m_engine->socketPath()));

    QSignalSpy finishedSpy(&backend, &DockerBackend::mutationFinished);
    backend.removeImage(QStringLiteral("sha256:aaaa"), false);
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);

    FakeEngine::RequestRecord request = m_engine->lastRequest();
    QCOMPARE(request.method, QStringLiteral("DELETE"));
    QCOMPARE(request.path, QStringLiteral("/v1.56/images/sha256:aaaa"));
    QCOMPARE(request.query, QStringLiteral("force=false"));

    backend.removeImage(QStringLiteral("sha256:aaaa"), true);
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 2, 10000);
    request = m_engine->lastRequest();
    QCOMPARE(request.query, QStringLiteral("force=true"));
    QCOMPARE(finishedSpy.at(1).at(2).value<Outcome>(), Outcome::Succeeded);
}

/*!
 * A mutation can be triggered before the version handshake finishes (e.g. Start clicked right after
 * the KCM opens): it must queue for the handshake instead of sending a request without a /v1.xx prefix.
 */
void DockerBackendFakeEngineTest::mutationWaitsForApiVersionHandshake()
{
    DockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(m_engine->socketPath()));

    QSignalSpy finishedSpy(&backend, &DockerBackend::mutationFinished);
    // No refreshAll first: the client does not know the API version yet
    QVERIFY(!backend.isLoading());
    backend.startContainer(kContainerId);
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);

    const FakeEngine::RequestRecord request = m_engine->lastRequest();
    QCOMPARE(request.method, QStringLiteral("POST"));
    QVERIFY2(request.path.startsWith(QStringLiteral("/v1.56/")), qPrintable(request.path));
    QCOMPARE(finishedSpy.at(0).at(2).value<Outcome>(), Outcome::Succeeded);
}


/*!
 * Concurrent pulls (ARCH_V4 §2.4): different references in flight at once stay independent:
 * cancelling one must not affect the other's state or outcome.
 */
void DockerBackendFakeEngineTest::concurrentPullsAreIndependent()
{
    m_engine->setPullChunkDelayMs(25);

    DockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(m_engine->socketPath()));

    QSet<QString> progressing;
    bool cancelledFirst = false;
    connect(&backend, &DockerBackend::imagePullProgress, this, [&](const ImagePullProgress &progress) {
        progressing.insert(progress.reference);
        if (progress.reference == QLatin1String("alpine:3.19") && !cancelledFirst) {
            cancelledFirst = true;
            backend.cancelImagePull(QStringLiteral("alpine:3.19"));
        }
    });

    QSignalSpy finishedSpy(&backend, &DockerBackend::mutationFinished);
    backend.pullImage(QStringLiteral("alpine:3.19"));
    backend.pullImage(QStringLiteral("busybox:latest"));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 2, 20000);

    QHash<QString, int> outcomeByTarget;
    for (const QList<QVariant> &call : finishedSpy) {
        outcomeByTarget.insert(call.at(1).toString(), int(call.at(2).value<Outcome>()));
    }
    QCOMPARE(outcomeByTarget.value(QStringLiteral("image:alpine:3.19")), int(Outcome::Cancelled));
    QCOMPARE(outcomeByTarget.value(QStringLiteral("image:busybox:latest")), int(Outcome::Succeeded));
    // Both really made progress (not one finishing before the other starts)
    QVERIFY(progressing.contains(QStringLiteral("alpine:3.19")));
    QVERIFY(progressing.contains(QStringLiteral("busybox:latest")));

    // A cancelled reference can be pulled again (no leftover state)
    backend.pullImage(QStringLiteral("alpine:3.19"));
    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() == 3, 20000);
    QCOMPARE(finishedSpy.at(2).at(2).value<Outcome>(), Outcome::Succeeded);
}

/*!
 * Network mutations (ARCH_V5_V8 §3.3): create uses a JSON body, remove uses DELETE.
 *
 * Create is the **first mutation with a request body** since phase 4 (all earlier ones used query
 * parameters), so this pins both "the body really went out" and "the keys match the Docker API".
 */
/*!
 * Network members can only be aggregated from the **container list** (user report: every bridge on
 * the network page showed 0 containers).
 *
 * Read-only evidence from a real daemon:
 *   - `Containers` in `GET /networks` **is empty** (the list endpoint does not fill it)
 *   - `GET /networks/{id}` fills it (6 for bridge)
 *   - every container in `GET /containers/json` carries `NetworkSettings.Networks` (with IP/MAC)
 * So "which containers joined this network" must be aggregated container-side; the fake engine pins
 * that shape: the network endpoint returns empty Containers, the container endpoint carries the
 * membership, and the final members must come from the container side.
 */
void DockerBackendFakeEngineTest::networkMembersComeFromTheContainerList()
{
    m_engine->setPathStatus(QStringLiteral("/networks"), 200, QByteArrayLiteral(R"([
        {"Name": "bridge", "Id": "bridge-id-00000000000000000000000000000000000000000000000000000000",
         "Driver": "bridge", "Scope": "local", "IPAM": {"Config": []}, "Labels": {}, "Containers": {}},
        {"Name": "app_default", "Id": "app-id-0000000000000000000000000000000000000000000000000000000000",
         "Driver": "bridge", "Scope": "local", "IPAM": {"Config": []}, "Labels": {}, "Containers": {}}
    ])"));
    m_engine->setPathStatus(QStringLiteral("/containers/json"), 200, QByteArrayLiteral(R"([
        {"Id": "cid-1", "Names": ["/web"], "Image": "alpine:latest", "ImageID": "sha256:a", "State": "running",
         "Status": "Up 5 minutes", "Created": 1700000000, "Ports": [],
         "NetworkSettings": {"Networks": {"app_default": {"NetworkID": "app-id-0000000000000000000000000000000000000000000000000000000000",
             "IPAddress": "172.18.0.2", "MacAddress": "02:42:ac:12:00:02", "GlobalIPv6Address": ""}}}},
        {"Id": "cid-2", "Names": ["/db"], "Image": "postgres:17", "ImageID": "sha256:b", "State": "running",
         "Status": "Up 4 minutes", "Created": 1700000001, "Ports": [],
         "NetworkSettings": {"Networks": {"bridge": {"NetworkID": "bridge-id-00000000000000000000000000000000000000000000000000000000",
             "IPAddress": "172.17.0.3", "MacAddress": "02:42:ac:11:00:03", "GlobalIPv6Address": ""}}}},
        {"Id": "cid-3", "Names": ["/worker"], "Image": "alpine:latest", "ImageID": "sha256:a", "State": "running",
         "Status": "Up 3 minutes", "Created": 1700000002, "Ports": [],
         "NetworkSettings": {"Networks": {"app_default": {"NetworkID": "app-id-0000000000000000000000000000000000000000000000000000000000",
             "IPAddress": "172.18.0.3", "MacAddress": "02:42:ac:12:00:03", "GlobalIPv6Address": ""}}}}
    ])"));

    DockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(m_engine->socketPath()));

    // Fetch containers first (the membership source), then networks
    QSignalSpy containersSpy(&backend, &DockerBackend::containersUpdated);
    backend.refreshContainers();
    QTRY_COMPARE_WITH_TIMEOUT(containersSpy.count(), 1, 10000);

    QSignalSpy networksSpy(&backend, &DockerBackend::networksUpdated);
    backend.refreshNetworks();
    QTRY_VERIFY_WITH_TIMEOUT(networksSpy.count() >= 1, 10000);

    const QList<Network> networks = backend.networks();
    QCOMPARE(networks.size(), 2);
    for (const Network &network : networks) {
        const QList<NetworkMember> members = network.members;
        QStringList names;
        for (const NetworkMember &member : members) {
            names.append(member.name);
        }
        names.sort();
        if (network.name == QLatin1String("app_default")) {
            QCOMPARE(names, QStringList({QStringLiteral("web"), QStringLiteral("worker")}));
            // Address and MAC must come along too (the detail page shows them)
            QVERIFY(!members.first().ipv4Address.isEmpty());
            QVERIFY(!members.first().macAddress.isEmpty());
        } else {
            QCOMPARE(names, QStringList {QStringLiteral("db")});
        }
    }

    // Reverse order: containers arrive later, members must still be filled in (idempotent recompute)
    DockerBackend lateBackend;
    lateBackend.setEndpoint(DockerEndpoint::unixSocket(m_engine->socketPath()));
    QSignalSpy lateNetworksSpy(&lateBackend, &DockerBackend::networksUpdated);
    lateBackend.refreshNetworks();
    QTRY_VERIFY_WITH_TIMEOUT(lateNetworksSpy.count() >= 1, 10000);
    QCOMPARE(lateBackend.networks().at(1).members.size(), 0); // no container data yet
    QSignalSpy lateContainersSpy(&lateBackend, &DockerBackend::containersUpdated);
    lateBackend.refreshContainers();
    QTRY_COMPARE_WITH_TIMEOUT(lateContainersSpy.count(), 1, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(lateBackend.networks().at(1).members.size(), 2, 10000);
}

void DockerBackendFakeEngineTest::networkCreateSendsJsonBodyAndRemoveUsesDelete()
{
    DockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(m_engine->socketPath()));
    m_engine->setPathStatus(QStringLiteral("/networks/create"), 201,
                            QByteArrayLiteral("{\"Id\":\"abc123\",\"Warning\":\"\"}"));
    m_engine->setPathStatus(QStringLiteral("/networks/abc123"), 204, QByteArray());

    QSignalSpy finishedSpy(&backend, &DockerBackend::mutationFinished);

    NetworkCreateRequest request;
    request.name = QStringLiteral("app_net");
    request.subnet = QStringLiteral("172.30.0.0/16");
    request.gateway = QStringLiteral("172.30.0.1");
    request.internal = true;
    request.labels.append({QStringLiteral("com.example.owner"), QStringLiteral("team-a")});
    backend.createNetwork(request);
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);

    QCOMPARE(finishedSpy.at(0).at(0).value<DockerBackendInterface::Mutation>(), DockerBackendInterface::Mutation::CreateNetwork);
    QCOMPARE(finishedSpy.at(0).at(1).toString(), QStringLiteral("network:app_net"));
    QCOMPARE(finishedSpy.at(0).at(2).value<DockerBackendInterface::MutationOutcome>(),
             DockerBackendInterface::MutationOutcome::Succeeded);

    const FakeEngine::RequestRecord created = m_engine->lastRequest();
    QCOMPARE(created.method, QStringLiteral("POST"));
    QCOMPARE(created.path, QStringLiteral("/v1.56/networks/create"));
    QVERIFY(created.headers.contains("Content-Type: application/json"));

    const QJsonObject body = QJsonDocument::fromJson(created.body).object();
    QCOMPARE(body.value(QStringLiteral("Name")).toString(), QStringLiteral("app_net"));
    QCOMPARE(body.value(QStringLiteral("Driver")).toString(), QStringLiteral("bridge"));
    QVERIFY(body.value(QStringLiteral("Internal")).toBool());
    QVERIFY(!body.value(QStringLiteral("Attachable")).toBool());
    QCOMPARE(body.value(QStringLiteral("Labels")).toObject().value(QStringLiteral("com.example.owner")).toString(),
             QStringLiteral("team-a"));
    const QJsonArray configs = body.value(QStringLiteral("IPAM")).toObject().value(QStringLiteral("Config")).toArray();
    QCOMPARE(configs.size(), 1);
    QCOMPARE(configs.at(0).toObject().value(QStringLiteral("Subnet")).toString(), QStringLiteral("172.30.0.0/16"));
    QCOMPARE(configs.at(0).toObject().value(QStringLiteral("Gateway")).toString(), QStringLiteral("172.30.0.1"));

    // Remove: DELETE /networks/{id}
    backend.removeNetwork(QStringLiteral("abc123"));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 2, 10000);
    QCOMPARE(finishedSpy.at(1).at(0).value<DockerBackendInterface::Mutation>(), DockerBackendInterface::Mutation::RemoveNetwork);
    const FakeEngine::RequestRecord removed = m_engine->lastRequest();
    QCOMPARE(removed.method, QStringLiteral("DELETE"));
    QCOMPARE(removed.path, QStringLiteral("/v1.56/networks/abc123"));
}

/*!
 * Container connect/disconnect to a network (ARCH_V5_V8 §3.4): both are POSTs with a JSON body.
 */
void DockerBackendFakeEngineTest::networkConnectAndDisconnectSendTheContainer()
{
    DockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(m_engine->socketPath()));
    m_engine->setPathStatus(QStringLiteral("/networks/net1/connect"), 200, QByteArrayLiteral("{}"));
    m_engine->setPathStatus(QStringLiteral("/networks/net1/disconnect"), 200, QByteArrayLiteral("{}"));

    QSignalSpy finishedSpy(&backend, &DockerBackend::mutationFinished);

    backend.connectNetwork(QStringLiteral("net1"), QStringLiteral("cid-1"), {QStringLiteral("app"), QStringLiteral(" api ")});
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);
    QCOMPARE(finishedSpy.at(0).at(0).value<DockerBackendInterface::Mutation>(),
             DockerBackendInterface::Mutation::ConnectNetwork);

    const FakeEngine::RequestRecord connected = m_engine->lastRequest();
    QCOMPARE(connected.method, QStringLiteral("POST"));
    QCOMPARE(connected.path, QStringLiteral("/v1.56/networks/net1/connect"));
    const QJsonObject connectBody = QJsonDocument::fromJson(connected.body).object();
    QCOMPARE(connectBody.value(QStringLiteral("Container")).toString(), QStringLiteral("cid-1"));
    const QJsonArray aliases = connectBody.value(QStringLiteral("EndpointConfig")).toObject().value(QStringLiteral("Aliases")).toArray();
    QCOMPARE(aliases.size(), 2);
    QCOMPARE(aliases.at(0).toString(), QStringLiteral("app"));
    QCOMPARE(aliases.at(1).toString(), QStringLiteral("api")); // trimmed

    backend.disconnectNetwork(QStringLiteral("net1"), QStringLiteral("cid-1"));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 2, 10000);
    const FakeEngine::RequestRecord disconnected = m_engine->lastRequest();
    QCOMPARE(disconnected.method, QStringLiteral("POST"));
    QCOMPARE(disconnected.path, QStringLiteral("/v1.56/networks/net1/disconnect"));
    const QJsonObject disconnectBody = QJsonDocument::fromJson(disconnected.body).object();
    QCOMPARE(disconnectBody.value(QStringLiteral("Container")).toString(), QStringLiteral("cid-1"));
    // force off by default: do not force-disconnect a network in use
    QVERIFY(!disconnectBody.value(QStringLiteral("Force")).toBool());
}


/*!
 * Volume list (ARCH_V5_V8 §3.5): request shape and parsing.
 *
 * Unlike `/networks` the payload is an **object** (`{Volumes, Warnings}`), and `Volumes` is null
 * when the list is empty.
 */
void DockerBackendFakeEngineTest::volumesAreListedFromTheEngine()
{
    const QByteArray payload = R"({
        "Volumes": [
            {"Name": "app_data", "Driver": "local",
             "Mountpoint": "/var/lib/docker/volumes/app_data/_data",
             "CreatedAt": "2026-09-16T17:44:09.395242226+08:00",
             "Scope": "local", "Labels": {"com.example.owner": "team-a"},
             "Options": {"type": "none"},
             "UsageData": {"Size": 4096, "RefCount": 2}}
        ],
        "Warnings": ["volume driver nfs is not available"]
    })";
    m_engine->setPathStatus(QStringLiteral("/volumes"), 200, payload);

    DockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(m_engine->socketPath()));
    QSignalSpy updatedSpy(&backend, &DockerBackend::volumesUpdated);
    QSignalSpy failureSpy(&backend, &DockerBackend::sectionFailed);

    backend.refreshVolumes();
    QTRY_COMPARE_WITH_TIMEOUT(updatedSpy.count(), 1, 10000);

    QCOMPARE(failureSpy.count(), 0);
    const QList<Volume> volumes = backend.volumes();
    QCOMPARE(volumes.size(), 1);
    QCOMPARE(volumes.first().name, QStringLiteral("app_data"));
    QVERIFY(volumes.first().isInUse());
    QCOMPARE(volumes.first().sizeBytes, 4096);

    FakeEngine::RequestRecord request = m_engine->lastRequest();
    QCOMPARE(request.method, QStringLiteral("GET"));
    QCOMPARE(request.path, QStringLiteral("/v1.56/volumes"));
    QVERIFY2(request.query.isEmpty(), "usage is included by default");

    // Disable usage stats: send no-usage (scanning usage is slow on large setups)
    backend.refreshVolumes(false);
    QTRY_COMPARE_WITH_TIMEOUT(updatedSpy.count(), 2, 10000);
    request = m_engine->lastRequest();
    QVERIFY(request.query.contains(QStringLiteral("no-usage=1")));
}


/*!
 * Container create (ARCH_V5_V8 §4.6): name in the query, body JSON from the mapping function,
 * id back via signal.
 */
void DockerBackendFakeEngineTest::createsAContainerWithNameInTheQuery()
{
    m_engine->setPathStatus(QStringLiteral("/containers/create"), 201,
                            QByteArrayLiteral("{\"Id\":\"new-container-id\",\"Warnings\":[]}"));

    DockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(m_engine->socketPath()));
    QSignalSpy createdSpy(&backend, &DockerBackend::containerCreated);
    QSignalSpy finishedSpy(&backend, &DockerBackend::mutationFinished);

    ContainerCreateRequest request;
    request.name = QStringLiteral("web");
    request.image = QStringLiteral("alpine:3.19");
    request.mounts = {{QStringLiteral("bind"), QStringLiteral("/srv/data"), QStringLiteral("/data"), true}};
    backend.createContainer(request);
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);

    QCOMPARE(createdSpy.count(), 1);
    QCOMPARE(createdSpy.at(0).at(0).toString(), QStringLiteral("new-container-id"));
    QCOMPARE(finishedSpy.at(0).at(2).value<DockerBackendInterface::MutationOutcome>(),
             DockerBackendInterface::MutationOutcome::Succeeded);

    const FakeEngine::RequestRecord record = m_engine->lastRequest();
    QCOMPARE(record.method, QStringLiteral("POST"));
    QCOMPARE(record.path, QStringLiteral("/v1.56/containers/create"));
    QCOMPARE(record.query, QStringLiteral("name=web")); // the name is in the query
    const QJsonObject body = QJsonDocument::fromJson(record.body).object();
    QCOMPARE(body.value(QStringLiteral("Image")).toString(), QStringLiteral("alpine:3.19"));
    QVERIFY2(!body.contains(QStringLiteral("Name")), "the name must not be in the body");

    // 201 without an Id: must count as failure, the UI must not think creation succeeded
    m_engine->setPathStatus(QStringLiteral("/containers/create"), 201, QByteArrayLiteral("{}"));
    backend.createContainer(request);
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 2, 10000);
    QCOMPARE(finishedSpy.at(1).at(2).value<DockerBackendInterface::MutationOutcome>(),
             DockerBackendInterface::MutationOutcome::Failed);
}

QTEST_GUILESS_MAIN(DockerBackendFakeEngineTest)

#include "tst_docker_backend_against_fake_engine.moc"
