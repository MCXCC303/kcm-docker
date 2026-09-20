/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
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
    // Chunked body upload: one chunk written, queue the next (this signal provides
    // backpressure; no busy loop)
    connect(m_socket, &QLocalSocket::bytesWritten, this, &DockerReply::onBytesWritten);
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
        // RFC 7230 token charset: alphanumerics plus !#$%&'*+-.^_`|~
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')
            || QByteArray("!#$%&'*+-.^_`|~").contains(ch);
    };
    for (const char ch : name) {
        if (!isTokenChar(ch)) {
            return false;
        }
    }
    for (const char ch : value) {
        // Reject CR/LF (header injection) and all other control characters
        if (ch == '\r' || ch == '\n' || (static_cast<unsigned char>(ch) < 0x20 && ch != '\t')) {
            return false;
        }
    }
    return true;
}

DockerReply::~DockerReply()
{
    if (m_bodyFile && m_bodyFile->isOpen()) {
        m_bodyFile->close();
    }
}

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
    // Two-stage timeout: wait for the first response, and only after the headers
    // switch to the streaming idle timeout
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

    if (!m_request.bodyFile.isEmpty()) {
        DockerError error;
        if (!openBodyFile(&error)) {
            // Must fail **later**: the caller connects finished() only after it got
            // the reply, so a synchronous emit would never reach it (symptom: the
            // build hangs when the context cannot be read)
            failLater(error);
            return;
        }
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
            failLater(DockerError(DockerError::Kind::PreconditionFailed, QStringLiteral("unsafe request header")));
            return;
        }
        request += it.key() + ": " + it.value() + "\r\n";
    }
    request += "Host: docker\r\n";
    request += "Accept: application/json\r\n";
    request += "User-Agent: kontainer/" KCM_DOCKER_VERSION "\r\n";
    const bool uploadsFile = !m_request.bodyFile.isEmpty();
    if (methodSendsBody(m_request.method)) {
        if (uploadsFile || !m_request.body.isEmpty()) {
            request += "Content-Type: " + m_request.bodyContentType + "\r\n";
        }
        // Declare the length even without a body (safer than omitting it)
        const qint64 length = uploadsFile ? QFileInfo(m_request.bodyFile).size() : m_request.body.size();
        request += "Content-Length: " + QByteArray::number(length) + "\r\n";
    }
    request += "Connection: close\r\n\r\n";

    m_socket->write(request);
    if (uploadsFile) {
        // Wider idle timeout while uploading: with a large context, "no progress" is
        // the anomaly
        if (m_request.uploadTimeoutMs > 0) {
            m_timer->start(m_request.uploadTimeoutMs);
        }
        writeNextBodyChunk();
        return;
    }
    m_socket->write(m_request.body);
    m_socket->flush();
}

void DockerReply::writeNextBodyChunk()
{
    if (isFinished()) {
        return;
    }
    if (!m_bodyFile) {
        return; // open failed (the error has already been emitted)
    }
    const qint64 chunk = qMin(kBodyChunkBytes, m_bodyRemaining);
    const QByteArray data = m_bodyFile->read(chunk);
    if (data.isEmpty() && m_bodyRemaining > 0) {
        fail(DockerError(DockerError::Kind::ConnectionFailed,
                         QStringLiteral("build context file shrank while uploading")));
        return;
    }
    m_bodyRemaining -= data.size();
    m_socket->write(data);
    m_socket->flush();
    if (m_bodyRemaining <= 0) {
        // Upload done: switch back to the response phase (streaming idle or first-response timeout)
        m_bodyFile->close();
        const int responseTimeout = m_request.timeoutMs > 0 ? m_request.timeoutMs : m_request.headersTimeoutMs;
        if (responseTimeout > 0) {
            m_timer->start(responseTimeout);
        } else {
            m_timer->stop();
        }
    }
}

bool DockerReply::openBodyFile(DockerError *error)
{
    m_bodyFile = new QFile(m_request.bodyFile, this);
    if (!m_bodyFile->open(QIODevice::ReadOnly)) {
        *error = DockerError(DockerError::Kind::PreconditionFailed,
                             QStringLiteral("cannot read the request body file: %1").arg(m_bodyFile->errorString()));
        return false;
    }
    m_bodyRemaining = m_bodyFile->size();
    return true;
}

void DockerReply::onBytesWritten(qint64 bytes)
{
    Q_UNUSED(bytes);
    if (isFinished() || !m_bodyFile) {
        return;
    }
    if (m_bodyRemaining > 0) {
        writeNextBodyChunk();
    }
}

void DockerReply::onReadyRead()
{
    if (isFinished()) {
        return;
    }
    m_parser->feed(m_socket->readAll());

    notifyStreamStarted();

    // Streaming response: the timeout means "no new data", restarted on every read.
    // timeoutMs == 0 disables it (a follow log stream may legitimately stay silent)
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
    // Normal end path for Connection: close
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
    // Headers arrived: switch to the streaming idle timeout (a pull may run long;
    // log follow has none)
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
    // 304 can only come from start (already running) / stop (already stopped): that
    // means "already in the target state", so success; the backend maps it to
    // MutationOutcome::Unchanged (ARCH_V4 §2.2.1).
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

    // Docker answers 400 with a clear message when the requested API version is unsupported
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
        // No headers yet -> the request never started (e.g. the engine cannot reach the registry)
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
    // Set the state before abort(): abort() synchronously fires disconnected/
    // errorOccurred, and those callbacks must see "already finished" to bail out
    // (otherwise they would succeed() and emit finished() a second time)
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

DockerReply *DockerClient::postFile(const QString &apiPath,
                                   const QUrlQuery &query,
                                   const QString &filePath,
                                   const QByteArray &contentType,
                                   int uploadTimeoutMs,
                                   int idleTimeoutMs,
                                   const QMap<QByteArray, QByteArray> &headers)
{
    return request(DockerReply::Method::Post,
                   apiPath,
                   query,
                   idleTimeoutMs > 0 ? idleTimeoutMs : m_timeoutMs,
                   /*streaming=*/true,
                   headers,
                   /*body=*/{},
                   filePath,
                   contentType,
                   uploadTimeoutMs);
}

DockerReply *DockerClient::del(const QString &apiPath, const QUrlQuery &query, int timeoutMs)
{
    return request(DockerReply::Method::Delete, apiPath, query, timeoutMs > 0 ? timeoutMs : m_timeoutMs, false);
}

DockerReply *DockerClient::getStream(const QString &apiPath, const QUrlQuery &query, int idleTimeoutMs)
{
    DockerReply *reply = request(DockerReply::Method::Get, apiPath, query, idleTimeoutMs, true);
    // First response still uses the normal timeout: fail fast when unreachable
    reply->setHeadersTimeoutMs(m_timeoutMs);
    return reply;
}

DockerReply *DockerClient::postStream(const QString &apiPath, const QUrlQuery &query, int idleTimeoutMs, const QMap<QByteArray, QByteArray> &headers)
{
    DockerReply *reply = request(DockerReply::Method::Post, apiPath, query, idleTimeoutMs > 0 ? idleTimeoutMs : m_timeoutMs, true, headers);
    // First response uses the normal request timeout: fail fast when the registry is
    // unreachable instead of waiting a whole minute
    reply->setHeadersTimeoutMs(m_timeoutMs);
    return reply;
}

DockerReply *DockerClient::request(DockerReply::Method method,
                                  const QString &apiPath,
                                  const QUrlQuery &query,
                                  int timeoutMs,
                                  bool streaming,
                                  const QMap<QByteArray, QByteArray> &headers,
                                  const QByteArray &body,
                                  const QString &bodyFile,
                                  const QByteArray &bodyContentType,
                                  int uploadTimeoutMs)
{
    QString path = apiPath;
    if (m_apiVersion.isValid()) {
        path = QLatin1Char('/') + m_apiVersion.pathPrefix() + apiPath;
    }

    // Log method and path only: never headers, payload, query or response body
    // (ARCH_V1 §27). The query may carry user data such as image references.
    qCDebug(kontainerApi) << methodName(method) << path;

    DockerReply::Request request;
    request.method = method;
    request.path = path;
    request.query = query;
    // timeoutMs <= 0 = no idle timeout (log follow); otherwise clamp to the minimum
    request.timeoutMs = timeoutMs <= 0 ? 0 : std::max(minimumTimeoutMs, timeoutMs);
    request.streaming = streaming;
    request.headers = headers;
    // The body must be in Request before start(): start() writes to the socket at once
    request.body = body;
    request.bodyFile = bodyFile;
    request.bodyContentType = bodyContentType;
    request.uploadTimeoutMs = uploadTimeoutMs;

    auto *reply = new DockerReply(m_endpoint, request, this);
    reply->start();
    return reply;
}

} // namespace Kontainer
