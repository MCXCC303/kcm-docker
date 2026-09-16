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
}

DockerReply::DockerReply(DockerEndpoint endpoint, QString path, QUrlQuery query, int timeoutMs, QObject *parent)
    : QObject(parent)
    , m_endpoint(std::move(endpoint))
    , m_path(std::move(path))
    , m_query(std::move(query))
    , m_timeoutMs(timeoutMs)
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
    m_timer->start(m_timeoutMs);
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
    request += "GET " + m_path.toUtf8();
    if (!m_query.isEmpty()) {
        request += "?" + m_query.toString(QUrl::FullyEncoded).toUtf8();
    }
    request += " HTTP/1.1\r\n";
    request += "Host: docker\r\n";
    request += "Accept: application/json\r\n";
    request += "User-Agent: kontainer/" KONTAINER_VERSION "\r\n";
    request += "Connection: close\r\n\r\n";

    m_socket->write(request);
    m_socket->flush();
}

void DockerReply::onReadyRead()
{
    if (isFinished()) {
        return;
    }
    m_parser->feed(m_socket->readAll());
    processBuffer();
}

void DockerReply::onDisconnected()
{
    if (isFinished()) {
        return;
    }
    // Connection: close 的正常结束路径
    m_parser->finishInput();
    processBuffer();
    if (!isFinished()) {
        fail(DockerError(DockerError::Kind::InvalidResponse, m_parser->errorString()));
    }
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
    if (status >= 200 && status < 300) {
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
    fail(DockerError(DockerError::Kind::Timeout,
                     QStringLiteral("no response within %1 ms").arg(m_timeoutMs)));
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
    return startRequest(apiPath, QUrlQuery());
}

DockerReply *DockerClient::get(const QString &apiPath, const QUrlQuery &query)
{
    QString path = apiPath;
    if (m_apiVersion.isValid()) {
        path = QLatin1Char('/') + m_apiVersion.pathPrefix() + apiPath;
    }
    return startRequest(path, query);
}

DockerReply *DockerClient::startRequest(const QString &path, const QUrlQuery &query)
{
    // 只记录方法与路径：不记录 header、payload 或响应体（ARCH_V1 §27）
    qCDebug(kontainerApi) << "GET" << path;
    auto *reply = new DockerReply(m_endpoint, path, query, m_timeoutMs, this);
    reply->start();
    return reply;
}

} // namespace Kontainer
