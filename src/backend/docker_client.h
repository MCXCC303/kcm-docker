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
 * 一次 Docker Engine 请求的句柄（语义类似 QNetworkReply）。
 *
 * 生命周期：DockerClient 的请求方法返回的指针归调用者所有（parent 为 client），
 * 请求结束（成功、失败或被取消）时发出一次 finished()，之后状态不再变化。
 */
class DockerReply : public QObject
{
    Q_OBJECT

public:
    /*! 四期只用到这三个方法（ARCH_V4 §2.2.1）。 */
    enum class Method {
        Get,
        Post,
        Delete,
    };
    Q_ENUM(Method)

    enum class State {
        Pending,
        Succeeded,
        Failed,
        /*! 被调用方主动取消：不是错误（ARCH_V4 §2.2.1）。 */
        Cancelled,
    };
    Q_ENUM(State)

    /*! 一次请求的完整描述（由 DockerClient 构造）。 */
    struct Request {
        Method method = Method::Get;
        QString path;
        QUrlQuery query;
        int timeoutMs = 0;
        /*! 流式响应：超时按「多久没有新数据」计算，而不是整个请求的总时长。 */
        bool streaming = false;
    };

    ~DockerReply() override;

    Method method() const
    {
        return m_request.method;
    }
    State state() const
    {
        return m_state;
    }
    bool isFinished() const
    {
        return m_state != State::Pending;
    }
    bool isStreaming() const
    {
        return m_request.streaming;
    }
    /*! 已解析出的 HTTP 状态码；尚未收到响应头时为 0。 */
    int httpStatus() const
    {
        return m_httpStatus;
    }
    /*! 完整响应体（请求结束后才有意义）。 */
    QByteArray body() const
    {
        return m_body;
    }
    /*! 取走尚未消费的响应体增量（流式请求用；body() 仍是全量）。 */
    QByteArray takeBody();
    DockerError error() const
    {
        return m_error;
    }

    /*!
     * 主动取消：关闭连接、状态置 Cancelled，并恰好发一次 finished()。
     * 取消后调用的 takeBody()/body() 仍能看到已收到的部分数据。
     */
    void cancel();

Q_SIGNALS:
    /*! 响应头已解析：httpStatus() 可用，流式请求据此先判断 2xx / 非 2xx。 */
    void streamStarted();
    /*! 收到了新的响应体增量（用 takeBody() 取走）。 */
    void bodyChunk();
    void finished();

private:
    friend class DockerClient;

    DockerReply(DockerEndpoint endpoint, Request request, QObject *parent = nullptr);

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
    /*! 首次拿到状态码时发出 streamStarted()。 */
    void notifyStreamStarted();
    static DockerError errorFromResponse(int httpStatus, const QByteArray &body);

    DockerEndpoint m_endpoint;
    Request m_request;

    QLocalSocket *m_socket = nullptr;
    QTimer *m_timer = nullptr;
    std::unique_ptr<HttpResponseParser> m_parser;

    State m_state = State::Pending;
    int m_httpStatus = 0;
    QByteArray m_body;
    DockerError m_error;
    /*! 已看到的响应体总量（用于判断有没有新数据要通知）。 */
    qsizetype m_seenBodyBytes = 0;
    bool m_streamStarted = false;
};

/*!
 * Docker Engine HTTP API 的薄封装（ARCH_V1 §10/§11，ARCH_V4 §2.2.1）。
 *
 * 四期起本类同时提供只读 GET 与写请求（POST / DELETE）。写请求只允许被
 * `docker_backend.cpp` 调用——这条约束由 tests/model/tst_source_conventions.cpp
 * 的 `mutationsHaveSingleChokePoint` 断言，避免写操作从某处顺手发出。
 *
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
    /*!
     * 写请求（无请求体，带 Content-Length: 0）。
     * `timeoutMs <= 0` 时使用客户端默认超时。
     */
    DockerReply *post(const QString &apiPath, const QUrlQuery &query = {}, int timeoutMs = 0);
    DockerReply *del(const QString &apiPath, const QUrlQuery &query = {}, int timeoutMs = 0);
    /*!
     * 流式写请求：超时按「多久没有新数据」计算（镜像拉取可以合法地跑很久）。
     */
    DockerReply *postStream(const QString &apiPath, const QUrlQuery &query = {}, int idleTimeoutMs = 0);

    /*! 通用入口：路径拼接（版本前缀）、超时与流式标记都在这里统一处理。 */
    DockerReply *request(DockerReply::Method method, const QString &apiPath, const QUrlQuery &query, int timeoutMs, bool streaming);

private:
    DockerEndpoint m_endpoint = DockerEndpoint::fromEnvironment();
    ApiVersion m_apiVersion;
    int m_timeoutMs = 10000;
};

} // namespace Kontainer
