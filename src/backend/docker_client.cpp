/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/docker_client.h"

#include "backend/http/http_response_parser.h"
#include "logging.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QTimer>

namespace Kontainer
{

namespace
{
constexpr int minimumTimeoutMs = 1000;

const char *methodName(DockerReply::Method method)
{
    switch (method) {
    case DockerReply::Method::Get:
        return "GET";
    case DockerReply::Method::Post:
        return "POST";
    case DockerReply::Method::Delete:
        return "DELETE";
    }
    return "GET";
}

bool methodSendsBody(DockerReply::Method method)
{
    return method == DockerReply::Method::Post || method == DockerReply::Method::Delete;
}
} // namespace

DockerReply::DockerReply(DockerEndpoint endpoint, Request request, QObject *parent)
    : QObject(parent)
    , m_endpoint(std::move(endpoint))
    , m_request(std::move(request))
{
    m_parser = std::make_unique<HttpResponseParser>();
    m_socket = new QLocalSocket(this);
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);

    connect(m_socket, &QLocalSocket::connected, this, &DockerReply::onConnected);
    connect(m_socket, &QLocalSocket::readyRead, this, &DockerReply::onReadyRead);
    connect(m_socket, &QLocalSocket::disconnected, this, &DockerReply::onDisconnected);
    connect(m_socket, &QLocalSocket::errorOccurred, this, [this](auto socketError) {
        onSocketError(int(socketError), m_socket->errorString());
    });
    connect(m_timer, &QTimer::timeout, this, &DockerReply::onTimeout);
}

bool DockerReply::isHeaderSafe(const QByteArray &name, const QByteArray &value)
{
    if (name.isEmpty()) {
        return false;
    }
    const auto isTokenChar = [](char ch) {
        // RFC 7230 的 token 字符集：字母数字与 !#$%&'*+-.^_`|~
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')
            || QByteArray("!#$%&'*+-.^_`|~").contains(ch);
    };
    for (const char ch : name) {
        if (!isTokenChar(ch)) {
            return false;
        }
    }
    for (const char ch : value) {
        // 禁止 CR/LF（请求头注入）与其余控制字符
        if (ch == '\r' || ch == '\n' || (static_cast<unsigned char>(ch) < 0x20 && ch != '\t')) {
            return false;
        }
    }
    return true;
}

DockerReply::~DockerReply() = default;

void DockerReply::start()
{
    if (!m_endpoint.isValid()) {
        failLater(DockerError(DockerError::Kind::DockerUnavailable, m_endpoint.problemDetail()));
        return;
    }
    if (!QFileInfo::exists(m_endpoint.socketPath())) {
        failLater(DockerError(DockerError::Kind::DockerUnavailable,
                              QStringLiteral("socket %1 does not exist").arg(m_endpoint.socketPath())));
        return;
    }
    // 两段超时：先等「首个响应」，收到响应头之后才按流式静默超时算
    const int firstResponseTimeout = m_request.headersTimeoutMs > 0 ? m_request.headersTimeoutMs : m_request.timeoutMs;
    if (firstResponseTimeout > 0) {
        m_timer->start(firstResponseTimeout);
    }
    m_socket->connectToServer(m_endpoint.socketPath());
}

void DockerReply::failLater(const DockerError &error)
{
    QTimer::singleShot(0, this, [this, error] {
        fail(error);
    });
}

void DockerReply::onConnected()
{
    if (isFinished()) {
        return;
    }

    QByteArray request;
    request += methodName(m_request.method);
    request += ' ';
    request += m_request.path.toUtf8();
    if (!m_request.query.isEmpty()) {
        request += "?" + m_request.query.toString(QUrl::FullyEncoded).toUtf8();
    }
    request += " HTTP/1.1\r\n";
    for (auto it = m_request.headers.constBegin(); it != m_request.headers.constEnd(); ++it) {
        if (!isHeaderSafe(it.key(), it.value())) {
            fail(DockerError(DockerError::Kind::PreconditionFailed, QStringLiteral("unsafe request header")));
            return;
        }
        request += it.key() + ": " + it.value() + "\r\n";
    }
    request += "Host: docker\r\n";
    request += "Accept: application/json\r\n";
    request += "User-Agent: kontainer/" KONTAINER_VERSION "\r\n";
    if (methodSendsBody(m_request.method)) {
        if (!m_request.body.isEmpty()) {
            request += "Content-Type: application/json\r\n";
        }
        // 没有请求体时也显式声明长度（比留空更稳妥）
        request += "Content-Length: " + QByteArray::number(m_request.body.size()) + "\r\n";
    }
    request += "Connection: close\r\n\r\n";
    request += m_request.body;

    m_socket->write(request);
    m_socket->flush();
}

void DockerReply::onReadyRead()
{
    if (isFinished()) {
        return;
    }
    m_parser->feed(m_socket->readAll());

    notifyStreamStarted();

    // 流式响应：超时语义是「多久没有新数据」，每次收到数据就重新计时。
    // timeoutMs == 0 表示不设静默超时（日志 follow 流可以合法地长时间静默）
    if (m_request.streaming && m_request.timeoutMs > 0) {
        m_timer->start(m_request.timeoutMs);
    }

    if (m_parser->body().size() != m_seenBodyBytes) {
        m_seenBodyBytes = m_parser->body().size();
        Q_EMIT bodyChunk();
    }

    processBuffer();
}

void DockerReply::onDisconnected()
{
    if (isFinished()) {
        return;
    }
    // Connection: close 的正常结束路径
    m_parser->finishInput();
    notifyStreamStarted();
    if (m_parser->body().size() != m_seenBodyBytes) {
        m_seenBodyBytes = m_parser->body().size();
        Q_EMIT bodyChunk();
    }
    processBuffer();
    if (!isFinished()) {
        fail(DockerError(DockerError::Kind::InvalidResponse, m_parser->errorString()));
    }
}

void DockerReply::notifyStreamStarted()
{
    if (m_streamStarted || m_parser->statusCode() == 0) {
        return;
    }
    m_streamStarted = true;
    // 响应头到了：切换到流式静默超时（拉取可以合法地跑很久；日志 follow 则不设超时）
    if (m_request.streaming && m_request.timeoutMs > 0) {
        m_timer->start(m_request.timeoutMs);
    }
    Q_EMIT streamStarted();
}

void DockerReply::processBuffer()
{
    if (isFinished()) {
        return;
    }
    if (m_parser->isFailed()) {
        fail(DockerError(DockerError::Kind::InvalidResponse, m_parser->errorString()));
        return;
    }
    if (!m_parser->isComplete()) {
        return;
    }

    const int status = m_parser->statusCode();
    const QByteArray body = m_parser->body();
    // 304 只可能来自 start（已运行）/ stop（已停止）：那是「已处于目标状态」，
    // 属于成功语义，由 backend 落成 MutationOutcome::Unchanged（ARCH_V4 §2.2.1）。
    if ((status >= 200 && status < 300) || status == 304) {
        succeed(status, body);
    } else {
        fail(errorFromResponse(status, body));
    }
}

DockerError DockerReply::errorFromResponse(int httpStatus, const QByteArray &body)
{
    QString apiMessage;
    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (document.isObject()) {
        apiMessage = document.object().value(QStringLiteral("message")).toString();
    }

    // Docker 在客户端请求的 API 版本不受支持时返回 400 + 明确说明
    if (httpStatus == 400
        && (apiMessage.contains(QLatin1String("client version"), Qt::CaseInsensitive)
            || apiMessage.contains(QLatin1String("too new"), Qt::CaseInsensitive)
            || apiMessage.contains(QLatin1String("too old"), Qt::CaseInsensitive))) {
        return DockerError(DockerError::Kind::ApiVersionMismatch, apiMessage, httpStatus);
    }
    return DockerError::fromHttpStatus(httpStatus, apiMessage);
}

void DockerReply::onSocketError(int socketError, const QString &socketErrorString)
{
    if (isFinished()) {
        return;
    }
    DockerError error = DockerError::fromSocketError(socketError, socketErrorString);
    if (error.kind() != DockerError::Kind::PermissionDenied && !QFileInfo::exists(m_endpoint.socketPath())) {
        error = DockerError(DockerError::Kind::DockerUnavailable,
                            QStringLiteral("socket %1 does not exist").arg(m_endpoint.socketPath()));
    }
    fail(error);
}

void DockerReply::onTimeout()
{
    if (isFinished()) {
        return;
    }
    if (m_request.streaming) {
        // 还没收到响应头 → 请求根本没开始（例如引擎联系不上镜像仓库）
        if (!m_streamStarted) {
            fail(DockerError(DockerError::Kind::Timeout,
                             QStringLiteral("no response headers within %1 ms").arg(m_request.headersTimeoutMs)));
            return;
        }
        fail(DockerError(DockerError::Kind::Timeout,
                         QStringLiteral("no data within %1 ms").arg(m_request.timeoutMs)));
        return;
    }
    fail(DockerError(DockerError::Kind::Timeout,
                     QStringLiteral("no response within %1 ms").arg(m_request.timeoutMs)));
}

QByteArray DockerReply::takeBody()
{
    return m_parser->takeBody();
}

void DockerReply::succeed(int httpStatus, QByteArray body)
{
    m_httpStatus = httpStatus;
    m_body = std::move(body);
    m_state = State::Succeeded;
    m_timer->stop();
    m_socket->abort();
    Q_EMIT finished();
}

void DockerReply::fail(const DockerError &error)
{
    m_error = error;
    m_state = State::Failed;
    m_timer->stop();
    m_socket->abort();
    Q_EMIT finished();
}

void DockerReply::cancel()
{
    if (isFinished()) {
        return;
    }
    // 先落状态再 abort：abort() 会同步触发 disconnected/errorOccurred，
    // 那些回调看到「已经结束」才会正确退出（否则会先 succeed() 再发第二次 finished()）
    m_state = State::Cancelled;
    m_timer->stop();
    m_socket->abort();
    Q_EMIT finished();
}

DockerClient::DockerClient(QObject *parent)
    : QObject(parent)
{
}

DockerClient::~DockerClient() = default;

void DockerClient::setTimeoutMs(int timeoutMs)
{
    m_timeoutMs = std::max(minimumTimeoutMs, timeoutMs);
}

DockerReply *DockerClient::getUnversioned(const QString &apiPath)
{
    return request(DockerReply::Method::Get, apiPath, QUrlQuery(), m_timeoutMs, false);
}

DockerReply *DockerClient::get(const QString &apiPath, const QUrlQuery &query)
{
    return request(DockerReply::Method::Get, apiPath, query, m_timeoutMs, false);
}

DockerReply *DockerClient::post(const QString &apiPath,
                               const QUrlQuery &query,
                               int timeoutMs,
                               const QMap<QByteArray, QByteArray> &headers,
                               const QByteArray &body)
{
    return request(DockerReply::Method::Post, apiPath, query, timeoutMs > 0 ? timeoutMs : m_timeoutMs, false, headers, body);
}

DockerReply *DockerClient::del(const QString &apiPath, const QUrlQuery &query, int timeoutMs)
{
    return request(DockerReply::Method::Delete, apiPath, query, timeoutMs > 0 ? timeoutMs : m_timeoutMs, false);
}

DockerReply *DockerClient::getStream(const QString &apiPath, const QUrlQuery &query, int idleTimeoutMs)
{
    DockerReply *reply = request(DockerReply::Method::Get, apiPath, query, idleTimeoutMs, true);
    // 首个响应仍用普通超时：连不上时快速失败，而不是干等
    reply->setHeadersTimeoutMs(m_timeoutMs);
    return reply;
}

DockerReply *DockerClient::postStream(const QString &apiPath, const QUrlQuery &query, int idleTimeoutMs, const QMap<QByteArray, QByteArray> &headers)
{
    DockerReply *reply = request(DockerReply::Method::Post, apiPath, query, idleTimeoutMs > 0 ? idleTimeoutMs : m_timeoutMs, true, headers);
    // 首个响应用普通请求超时：仓库不可达时快速失败，而不是干等一分钟
    reply->setHeadersTimeoutMs(m_timeoutMs);
    return reply;
}

DockerReply *DockerClient::request(DockerReply::Method method,
                                  const QString &apiPath,
                                  const QUrlQuery &query,
                                  int timeoutMs,
                                  bool streaming,
                                  const QMap<QByteArray, QByteArray> &headers,
                                  const QByteArray &body)
{
    QString path = apiPath;
    if (m_apiVersion.isValid()) {
        path = QLatin1Char('/') + m_apiVersion.pathPrefix() + apiPath;
    }

    // 只记录方法与路径：不记录 header、payload、query 或响应体（ARCH_V1 §27）。
    // query 里可能含镜像引用等用户数据，因此不进日志。
    qCDebug(kontainerApi) << methodName(method) << path;

    DockerReply::Request request;
    request.method = method;
    request.path = path;
    request.query = query;
    // timeoutMs <= 0 = 不设静默超时（日志 follow 流）；其余取最小值保护
    request.timeoutMs = timeoutMs <= 0 ? 0 : std::max(minimumTimeoutMs, timeoutMs);
    request.streaming = streaming;
    request.headers = headers;
    // 请求体必须在 start() 之前放进 Request：start() 会立刻把请求写进 socket
    request.body = body;

    auto *reply = new DockerReply(m_endpoint, request, this);
    reply->start();
    return reply;
}

} // namespace Kontainer
