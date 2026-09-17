/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/docker_api_version.h"
#include "backend/docker_endpoint.h"
#include "backend/docker_error.h"
#include "refresh_policy.h"

#include <QMap>
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
    /*!
     * 请求头是否安全（名字与值都不得含 CR/LF，值里也不能有裸控制字符）。
     *
     * `X-Registry-Auth` 的值由 `RegistryAuth::encode()` 生成、可控，但凭据最终
     * 来自用户输入或钱包，因此在真正写进 socket 之前必须再挡一次。
     */
    static bool isHeaderSafe(const QByteArray &name, const QByteArray &value);

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
        /*!
         * 请求体（JSON）。
         *
         * 四期的写操作都靠 query 参数；六期创建网络需要 `POST /networks/create`
         * 带 JSON 体，因此在这里按需加上（并统一声明 `Content-Type: application/json`）。
         */
        QByteArray body;
        /*!
         * 流式请求的「首个响应」超时：在收到响应头之前用这个值。
         *
         * 为什么需要两段超时：拉取镜像时引擎要先联系镜像仓库，如果仓库不可达
         * （网络 / 代理 / IPv6 没有出口），引擎会**一个字节都不回**。
         * 这时用 60 秒的静默超时太久了——用户看到的只是「点了没反应」；
         * 10 秒内没有响应头就判定「拉取没能开始」，给出可操作的提示（ARCH_V4 §2.2.1）。
         */
        int headersTimeoutMs = 0;
        /*! 流式响应：超时按「多久没有新数据」计算，而不是整个请求的总时长。 */
        bool streaming = false;
        /*!
         * 额外请求头（例如 `X-Registry-Auth`）。
         *
         * 值是 base64url，本来就不会含 CR/LF；但这里仍然在发送前做一次校验
         * （见 `isHeaderSafe()`）——手写 HTTP 请求不能让任何来源拼出换行，
         * 否则就是请求头注入。
         */
        QMap<QByteArray, QByteArray> headers;
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

    /*! 流式请求：收到响应头之前使用的超时（由 DockerClient 设置）。 */
    void setHeadersTimeoutMs(int timeoutMs)
    {
        m_request.headersTimeoutMs = timeoutMs;
    }

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
    DockerReply *post(const QString &apiPath,
                      const QUrlQuery &query = {},
                      int timeoutMs = 0,
                      const QMap<QByteArray, QByteArray> &headers = {},
                      const QByteArray &body = {});
    DockerReply *del(const QString &apiPath, const QUrlQuery &query = {}, int timeoutMs = 0);
    /*!
     * 流式读请求（日志跟随是 GET）。
     *
     * `idleTimeoutMs <= 0` 表示**不设静默超时**：`follow=1` 的日志流可以合法地
     * 长时间没有数据（ARCH_V5_V8 §3.1.1），只能靠取消或页面生命周期结束。
     */
    DockerReply *getStream(const QString &apiPath, const QUrlQuery &query = {}, int idleTimeoutMs = 0);

    /*!
     * 流式写请求：超时按「多久没有新数据」计算（镜像拉取可以合法地跑很久）。
     */
    DockerReply *postStream(const QString &apiPath, const QUrlQuery &query = {}, int idleTimeoutMs = 0, const QMap<QByteArray, QByteArray> &headers = {});

    /*! 通用入口：路径拼接（版本前缀）、超时与流式标记都在这里统一处理。 */
    DockerReply *request(DockerReply::Method method,
                         const QString &apiPath,
                         const QUrlQuery &query,
                         int timeoutMs,
                         bool streaming,
                         const QMap<QByteArray, QByteArray> &headers = {},
                         const QByteArray &body = {});

private:
    DockerEndpoint m_endpoint = DockerEndpoint::fromEnvironment();
    ApiVersion m_apiVersion;
    int m_timeoutMs = 10000;
};

} // namespace Kontainer
