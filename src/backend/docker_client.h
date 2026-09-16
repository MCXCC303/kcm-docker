/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/docker_api_version.h"
#include "backend/docker_endpoint.h"
#include "backend/docker_error.h"
#include "refresh_policy.h"

#include <QObject>
#include <QUrlQuery>

#include <memory>

class QLocalSocket;
class QTimer;

namespace Kontainer
{

class HttpResponseParser;

/*!
 * 一次只读 GET 请求的句柄（语义类似 QNetworkReply）。
 *
 * 生命周期：DockerClient::get*() 返回的指针归调用者所有（parent 为 client），
 * 请求结束（成功或失败）时发出一次 finished()，之后状态不再变化。
 */
class DockerReply : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Pending,
        Succeeded,
        Failed,
    };
    Q_ENUM(State)

    ~DockerReply() override;

    State state() const
    {
        return m_state;
    }
    bool isFinished() const
    {
        return m_state != State::Pending;
    }
    int httpStatus() const
    {
        return m_httpStatus;
    }
    /*! 已完成解码的响应体（chunked 已合并）。 */
    QByteArray body() const
    {
        return m_body;
    }
    DockerError error() const
    {
        return m_error;
    }

Q_SIGNALS:
    void finished();

private:
    friend class DockerClient;

    DockerReply(DockerEndpoint endpoint, QString path, QUrlQuery query, int timeoutMs, QObject *parent = nullptr);

    void start();
    void onConnected();
    void onReadyRead();
    void onDisconnected();
    void onSocketError(int socketError, const QString &socketErrorString);
    void onTimeout();

    void succeed(int httpStatus, QByteArray body);
    void fail(const DockerError &error);
    /*!
     * 延迟到下一个事件循环再失败的路径。
     *
     * start() 可能在调用方 connect(finished) 之前就发现错误（endpoint 无效、
     * socket 不存在），此时必须异步发出信号，否则请求会永远悬挂。
     */
    void failLater(const DockerError &error);
    void processBuffer();
    static DockerError errorFromResponse(int httpStatus, const QByteArray &body);

    DockerEndpoint m_endpoint;
    QString m_path;
    QUrlQuery m_query;
    int m_timeoutMs = int(std::chrono::duration_cast<std::chrono::milliseconds>(RefreshPolicy::kRequestTimeout).count());

    QLocalSocket *m_socket = nullptr;
    QTimer *m_timer = nullptr;
    std::unique_ptr<HttpResponseParser> m_parser;

    State m_state = State::Pending;
    int m_httpStatus = 0;
    QByteArray m_body;
    DockerError m_error;
};

/*!
 * Docker Engine HTTP API 的薄封装（ARCH_V1 §10/§11）。
 *
 * 只提供 GET：本类没有任何写方法，也不对外暴露任意 HTTP 动词。
 * 所有 I/O 都是异步的（QLocalSocket + 事件循环），UI 线程永不阻塞（§18）。
 * API 版本前缀集中在这里通过 ApiVersion 拼接（§8）。
 */
class DockerClient : public QObject
{
    Q_OBJECT

public:
    explicit DockerClient(QObject *parent = nullptr);
    ~DockerClient() override;

    DockerEndpoint endpoint() const
    {
        return m_endpoint;
    }
    void setEndpoint(const DockerEndpoint &endpoint)
    {
        m_endpoint = endpoint;
    }

    ApiVersion apiVersion() const
    {
        return m_apiVersion;
    }
    bool hasApiVersion() const
    {
        return m_apiVersion.isValid();
    }
    void setApiVersion(const ApiVersion &version)
    {
        m_apiVersion = version;
    }
    void clearApiVersion()
    {
        m_apiVersion = ApiVersion();
    }

    int timeoutMs() const
    {
        return m_timeoutMs;
    }
    void setTimeoutMs(int timeoutMs);

    /*! 未版本化请求，仅用于 /_ping 与 /version 的版本协商。 */
    DockerReply *getUnversioned(const QString &apiPath);
    /*! 常规只读请求；已知 API 版本时自动加 v1.xx 前缀。 */
    DockerReply *get(const QString &apiPath, const QUrlQuery &query = {});

private:
    DockerReply *startRequest(const QString &path, const QUrlQuery &query);

    DockerEndpoint m_endpoint = DockerEndpoint::fromEnvironment();
    ApiVersion m_apiVersion;
    int m_timeoutMs = 10000;
};

} // namespace Kontainer
