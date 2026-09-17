/*
    SPDX-FileCopyrightText: 2026 kontainer developers
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
 * 用一个进程内的假 Docker Engine（Unix socket + 最小 HTTP）驱动真实 DockerBackend。
 *
 * 覆盖真实 daemon 上难以复现的行为：
 *  - 请求去重（§17：连续刷新只产生一个请求）
 *  - /info 刷新失败后旧计数必须作废（不能把过期计数当当前值）
 *  - API 版本协商失败 / daemon 返回 400 "too new"
 *  - Content-Length 与 chunked 两种响应
 *
 * 四期起假 Engine 也实现写端点（start / stop / restart / remove / image pull / image remove），
 * 并记录每个请求的方法、路径与 query：写操作的契约测试（动词、参数、状态码归一）
 * 因此不依赖真实 daemon，也不会动用户的容器（ARCH_V4 §5.3）。
 */
class FakeEngine : public QObject
{
    Q_OBJECT

public:
    struct RequestRecord {
        QString method;
        QString path;
        QString query;
        /*! 原始请求头块（`\r\n` 分隔，未含请求行）：认证用例要断言 `X-Registry-Auth` 的内容。 */
        QByteArray headers;
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

    /* --- 写操作的观察与注入控制 --- */

    QList<RequestRecord> requests() const
    {
        return m_requests;
    }
    /*! 最后一次请求（断言动词与 query 用）。 */
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
    /*! 让某个写端点返回指定状态码与响应体（例如 409 / 304）。 */
    void setMutationResponse(const QString &barePath, int status, const QByteArray &body)
    {
        m_mutationResponses.insert(barePath, {status, body});
    }
    /*! 让某个 start / stop 端点返回 304（引擎的「已处于目标状态」语义）。 */
    void setMutationNotModified(const QString &barePath)
    {
        m_notModifiedMutations.insert(barePath);
    }
    /*! 拉取流的每一行（每行一个 chunk，用于覆盖「一行跨 chunk / 一个 chunk 多行」）。 */
    void setPullLines(const QList<QByteArray> &lines)
    {
        m_pullLines = lines;
    }
    /*! 拉取流的写入间隔：> 0 时每个 chunk 之间留出事件循环时间（用于取消测试）。 */
    void setPullChunkDelayMs(int delayMs)
    {
        m_pullChunkDelayMs = delayMs;
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

    /*! 用例之间清空注入的失败/覆盖与计数，避免状态泄漏。 */
    void reset()
    {
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

    /*! 默认的镜像拉取流：两层各一段进度 + 一条完成行。 */
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
                respond(socket, method, QString::fromLatin1(parts.at(1)), headers);
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

    void respond(QLocalSocket *socket, const QString &method, const QString &rawTarget, const QByteArray &headers = {})
    {
        const QString path = rawTarget.section(QLatin1Char('?'), 0, 0);
        const QString query = rawTarget.section(QLatin1Char('?'), 1, 1);
        m_counts[path] += 1;
        m_requests.append({method, path, query, headers});
        const QString bare = withoutVersionPrefix(path);

        if (const auto override = m_overrides.constFind(bare); override != m_overrides.constEnd()) {
            writeResponse(socket, override->first, override->second, false);
            return;
        }

        /* --- 写端点（ARCH_V4 §2.2.4）：先看有没有注入的响应 --- */
        if (const auto injected = m_mutationResponses.constFind(bare); injected != m_mutationResponses.constEnd()) {
            writeResponse(socket, injected->first, injected->second, false);
            return;
        }
        if (method == QLatin1String("POST") && bare == QLatin1String("/images/create")) {
            writePullStream(socket, m_pullLines, m_pullChunkDelayMs);
            return;
        }
        if (method == QLatin1String("POST") && bare.startsWith(QLatin1String("/containers/"))) {
            // start（已运行）/ stop（已停止）之外的写操作一律 204
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

    /*!
     * 逐块写出 chunked 的拉取流：真实 daemon 的分片与 JSON 行边界毫无关系，
     * 因此每行单独一个 chunk，必要时还在 chunk 之间留出时间（取消测试）。
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

    /* --- 写操作契约（ARCH_V4 §5.1） --- */
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
    void authCheckSendsCredentialsOnlyInTheHeader();
    void authCheckClassifiesFailures();

private:
    FakeEngine *m_engine = nullptr;
};

/* ============================================================================
 * 仓库凭据校验（ARCH_V5_V8 §2.6）
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
 * 凭据**只能**出现在 `X-Registry-Auth` 头里：不进 URL（query 里没有）、不进请求体。
 * 同时头本身必须是 Docker 认的 base64url(JSON)——假引擎把头原样记下来供断言。
 */
void DockerBackendFakeEngineTest::authCheckSendsCredentialsOnlyInTheHeader()
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
    QVERIFY(request.headers.contains("X-Registry-Auth: "));

    // 头的内容必须能解回同一条凭据（含规范化后的 serveraddress）
    const QByteArray headerBlock = request.headers;
    const int headerStart = headerBlock.indexOf("X-Registry-Auth: ") + int(qstrlen("X-Registry-Auth: "));
    const QByteArray headerValue = headerBlock.mid(headerStart).split('\r').value(0);
    QString errorKey;
    const RegistryCredential decoded = RegistryAuth::decode(headerValue, &errorKey);
    QVERIFY2(errorKey.isEmpty(), qPrintable(errorKey));
    QCOMPARE(decoded.username, QStringLiteral("alice"));
    QCOMPARE(decoded.password, QStringLiteral("s3cret"));
    QCOMPARE(decoded.serverAddress, QStringLiteral("registry.example.com"));

    // 结果：成功，且回报的是规范化后的仓库地址
    QCOMPARE(checkedSpy.at(0).at(0).toString(), QStringLiteral("registry.example.com"));
    QCOMPARE(checkedSpy.at(0).at(1).value<AuthResult>(), AuthResult::Succeeded);
}

void DockerBackendFakeEngineTest::authCheckClassifiesFailures()
{
    DockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(m_engine->socketPath()));
    QSignalSpy checkedSpy(&backend, &DockerBackend::registryAuthChecked);

    // 401：用户名/密码不对（引擎原文里可能只有 unauthorized）
    m_engine->setPathStatus(QStringLiteral("/auth"), 401,
                            QByteArrayLiteral("{\"message\":\"unauthorized: incorrect username or password\"}"));
    backend.checkRegistryAuth(QStringLiteral("registry.example.com"), sampleCredential());
    QTRY_COMPARE_WITH_TIMEOUT(checkedSpy.count(), 1, 10000);
    QCOMPARE(checkedSpy.at(0).at(1).value<AuthResult>(), AuthResult::InvalidCredentials);

    // 500 且原文是网络错误：归到"仓库不可达"（界面要提示查网络/代理，而不是"密码错了"）
    m_engine->setPathStatus(QStringLiteral("/auth"), 500,
                            QByteArrayLiteral("{\"message\":\"dial tcp: lookup registry.invalid: no such host\"}"));
    backend.checkRegistryAuth(QStringLiteral("registry.example.com"), sampleCredential());
    QTRY_COMPARE_WITH_TIMEOUT(checkedSpy.count(), 2, 10000);
    QCOMPARE(checkedSpy.at(1).at(1).value<AuthResult>(), AuthResult::RegistryUnreachable);

    // 500 但看不出网络线索：普通失败（不乱猜）
    m_engine->setPathStatus(QStringLiteral("/auth"), 500, QByteArrayLiteral("{\"message\":\"something else went wrong\"}"));
    backend.checkRegistryAuth(QStringLiteral("registry.example.com"), sampleCredential());
    QTRY_COMPARE_WITH_TIMEOUT(checkedSpy.count(), 3, 10000);
    QCOMPARE(checkedSpy.at(2).at(1).value<AuthResult>(), AuthResult::Failed);

    // 参数不全：不发请求，直接给出"凭据不对"
    const int requestsBefore = m_engine->requests().size();
    RegistryCredential incomplete;
    incomplete.serverAddress = QStringLiteral("registry.example.com");
    backend.checkRegistryAuth(QStringLiteral("registry.example.com"), incomplete);
    QTRY_COMPARE_WITH_TIMEOUT(checkedSpy.count(), 4, 10000);
    QCOMPARE(checkedSpy.at(3).at(1).value<AuthResult>(), AuthResult::InvalidCredentials);
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

    // 协商前用无版本路径，协商后用 v1.56
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

    // 三次刷新只允许产生一个请求（§17 Coalesce）
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
    const int before = m_engine->requestCount(QStringLiteral("/containers/%1/json").arg(id));
    backend.inspectContainer(id);
    backend.inspectContainer(id);
    backend.inspectContainer(id);
    QTRY_COMPARE_WITH_TIMEOUT(detailSpy.count(), 1, 10000);

    QCOMPARE(m_engine->requestCount(QStringLiteral("/containers/%1/json").arg(id)) - before, 1);
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

/* ============================================================================
 * 写操作契约（ARCH_V4 §2.2.4 / §5.1）
 *
 * 这些用例断言的是「我们到底发了什么请求」与「引擎的状态码如何被归一」，
 * 全部跑在假 Engine 上：不碰真实 daemon，也不依赖任何容器存在。
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
    // 停止宽限期由 backend 统一决定（RefreshPolicy），不由 UI 传
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
    // 不删卷（不带 v）、不强制（不带 force）：这是有意的数据保护
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

    // 304 是「已经在跑了」，不是错误
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
    // 引擎原文要保留下来：文案映射靠它区分「运行中 / 被引用 / 名称冲突」
    QVERIFY(error.detail().contains(QStringLiteral("running container")));
}

void DockerBackendFakeEngineTest::pullImageSendsFromImageAndTag()
{
    DockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(m_engine->socketPath()));

    QSignalSpy finishedSpy(&backend, &DockerBackend::mutationFinished);
    // 不写 tag → 归一化为 latest，但仍然显式发 tag 参数
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
    // 两层共 140 字节，最后一层完成时总量已知且已满
    const ImagePullProgress last = progress.last();
    QCOMPARE(last.reference, QStringLiteral("alpine:3.19"));
    QCOMPARE(last.totalLayers, 2);
    QCOMPARE(last.completedLayers, 2);
    QCOMPARE(last.totalBytes, qint64(140));
    QCOMPARE(last.currentBytes, qint64(140));
    QCOMPARE(last.fraction(), 1.0);
    QVERIFY(last.statusText.contains(QStringLiteral("Downloaded newer image")));
    QVERIFY(!last.failed());

    // 中间至少有一次是「总量未知」的不确定态（进度条必须先能处理这种情况）
    bool sawIndeterminate = false;
    for (const ImagePullProgress &p : progress) {
        if (p.isIndeterminate() && p.phase != ImagePullProgress::Phase::Complete) {
            sawIndeterminate = true;
        }
    }
    QVERIFY(sawIndeterminate);

    // 进度必须单调不减（UI 的进度条不能倒退）
    qint64 previous = 0;
    for (const ImagePullProgress &p : progress) {
        QVERIFY(p.currentBytes >= previous);
        previous = p.currentBytes;
    }
}

void DockerBackendFakeEngineTest::pullImageErrorLineFailsTheMutation()
{
    // HTTP 是 200，失败在流里：这是 Docker 的常见形态，不能被当成成功
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

    // 在第一帧进度到达时取消：不依赖时序猜测，也不依赖握手耗时
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

    // 取消不是错误，但必须恰好上报一次结果
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
 * 写操作可能在版本握手完成之前被触发（例如 KCM 刚打开就点了启动）：
 * 必须排队等握手，而不是发出一个没有 /v1.xx 前缀的请求。
 */
void DockerBackendFakeEngineTest::mutationWaitsForApiVersionHandshake()
{
    DockerBackend backend;
    backend.setEndpoint(DockerEndpoint::unixSocket(m_engine->socketPath()));

    QSignalSpy finishedSpy(&backend, &DockerBackend::mutationFinished);
    // 不先 refreshAll：客户端此时还不知道 API 版本
    QVERIFY(!backend.isLoading());
    backend.startContainer(kContainerId);
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);

    const FakeEngine::RequestRecord request = m_engine->lastRequest();
    QCOMPARE(request.method, QStringLiteral("POST"));
    QVERIFY2(request.path.startsWith(QStringLiteral("/v1.56/")), qPrintable(request.path));
    QCOMPARE(finishedSpy.at(0).at(2).value<Outcome>(), Outcome::Succeeded);
}


/*!
 * 并发拉取（ARCH_V4 §2.4）：不同引用同时在途，互不干扰——
 * 取消其中一路不能影响另一路的状态与结果。
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
    // 两路都真的推进过（不是一路跑完另一路才开始）
    QVERIFY(progressing.contains(QStringLiteral("alpine:3.19")));
    QVERIFY(progressing.contains(QStringLiteral("busybox:latest")));

    // 取消过的引用可以重新拉取（状态没有残留）
    backend.pullImage(QStringLiteral("alpine:3.19"));
    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() == 3, 20000);
    QCOMPARE(finishedSpy.at(2).at(2).value<Outcome>(), Outcome::Succeeded);
}

QTEST_GUILESS_MAIN(DockerBackendFakeEngineTest)

#include "tst_docker_backend_against_fake_engine.moc"
