/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/docker_api_version.h"
#include "backend/docker_endpoint.h"
#include "backend/docker_error.h"
#include "refresh_policy.h"

#include <QFile>
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
 * Handle for one Docker Engine request (semantically like QNetworkReply).
 *
 * Ownership: the pointer returned by the DockerClient request methods belongs to the
 * caller (parent = client). finished() is emitted exactly once when the request ends
 * (success, failure or cancel); the state is final afterwards.
 */
class DockerReply : public QObject
{
    Q_OBJECT

public:
    /*!
     * Is the header safe? Name and value must be free of CR/LF, and the value may
     * not contain bare control characters either.
     *
     * `X-Registry-Auth` comes from `RegistryAuth::encode()`, but the credentials
     * originate from user input or the wallet, so re-check before writing to the
     * socket.
     */
    static bool isHeaderSafe(const QByteArray &name, const QByteArray &value);

    /*! Phase 4 uses only these three methods (ARCH_V4 §2.2.1). */
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
        /*! Cancelled by the caller: not an error (ARCH_V4 §2.2.1). */
        Cancelled,
    };
    Q_ENUM(State)

    /*! Full description of one request (built by DockerClient). */
    struct Request {
        Method method = Method::Get;
        QString path;
        QUrlQuery query;
        int timeoutMs = 0;
        /*! Idle timeout while uploading the request body (0 = reuse timeoutMs). */
        int uploadTimeoutMs = 0;
        /*!
         * Request body (JSON).
         *
         * Phase 4 writes everything via query parameters; phase 6 creates networks
         * with `POST /networks/create`, which needs a JSON body (declared as
         * `Content-Type: application/json`).
         */
        QByteArray body;
        /*!
         * Request body streamed from a **file** (the phase 8 image build tar context).
         *
         * Mutually exclusive with `body`: `Content-Length` comes from the file size
         * and the data is written in chunks (a context of tens or hundreds of MB must
         * not land in memory at once). The caller owns the file; it must stay alive
         * for the whole upload.
         */
        QString bodyFile;
        /*! Content type of the upload (default application/json; build context is application/x-tar). */
        QByteArray bodyContentType = QByteArrayLiteral("application/json");
        /*!
         * "First response" timeout for streaming requests: used until response
         * headers arrive.
         *
         * Why two timeouts: a pull makes the engine contact the registry first. If
         * the registry is unreachable (network / proxy / no IPv6 route), the engine
         * sends **not a single byte**. A 60 s idle timeout is far too long there,
         * since the user only sees "clicked, nothing happened"; no headers within
         * 10 s means the pull never started, which yields an actionable hint
         * (ARCH_V4 §2.2.1).
         */
        int headersTimeoutMs = 0;
        /*! Streaming response: timeout counts idle time, not total request duration. */
        bool streaming = false;
        /*!
         * Extra request headers (e.g. `X-Registry-Auth`).
         *
         * The value is base64url and cannot contain CR/LF, but it is still validated
         * before sending (see `isHeaderSafe()`): a hand-written HTTP request must not
         * let any source inject a line break, which would be header injection.
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
    /*! Parsed HTTP status code; 0 until response headers arrive. */
    int httpStatus() const
    {
        return m_httpStatus;
    }
    /*! Full response body (meaningful once the request finished). */
    QByteArray body() const
    {
        return m_body;
    }
    /*! Take the unconsumed response body delta (streaming; body() still holds all of it). */
    QByteArray takeBody();
    DockerError error() const
    {
        return m_error;
    }

    /*!
     * Cancel: closes the connection, sets state to Cancelled and emits finished()
     * exactly once. takeBody()/body() still return the data received so far.
     */
    void cancel();

    /*! Idle timeout for the upload phase: the streaming idle timeout must not kill an upload. */
    void setUploadTimeoutMs(int timeoutMs)
    {
        m_request.uploadTimeoutMs = timeoutMs;
    }

    /*! Body comes from a file (`postFile`): opened and chunk-written after start(). */
    void setBodyFile(const QString &filePath)
    {
        m_request.bodyFile = filePath;
    }

    /*! Content type of the request body. */
    void setBodyContentType(const QByteArray &contentType)
    {
        m_request.bodyContentType = contentType;
    }

    /*! Streaming request: timeout used before response headers arrive (set by DockerClient). */
    void setHeadersTimeoutMs(int timeoutMs)
    {
        m_request.headersTimeoutMs = timeoutMs;
    }

Q_SIGNALS:
    /*! Response headers parsed: httpStatus() is valid; streams check 2xx / non-2xx here. */
    void streamStarted();
    /*! A new response body delta arrived (take it with takeBody()). */
    void bodyChunk();
    void finished();

private:
    friend class DockerClient;

    DockerReply(DockerEndpoint endpoint, Request request, QObject *parent = nullptr);

    void start();
    void onConnected();
    /*! Continue the chunked upload; bytesWritten drives it until the response timeout resumes. */
    void writeNextBodyChunk();
    /*! One chunk written: queue the next one (backpressure). */
    void onBytesWritten(qint64 bytes);
    /*! Open the file to upload; on failure returns an error (the caller emits it). */
    bool openBodyFile(DockerError *error);
    void onReadyRead();
    void onDisconnected();
    void onSocketError(int socketError, const QString &socketErrorString);
    void onTimeout();

    void succeed(int httpStatus, QByteArray body);
    void fail(const DockerError &error);
    /*!
     * Fail on the next event loop iteration.
     *
     * start() can already hit an error (invalid endpoint, missing socket) before the
     * caller connects finished(); emitting synchronously would leave the request
     * hanging forever.
     */
    void failLater(const DockerError &error);
    void processBuffer();
    /*! Emit streamStarted() the first time a status code is known. */
    void notifyStreamStarted();
    static DockerError errorFromResponse(int httpStatus, const QByteArray &body);

    DockerEndpoint m_endpoint;
    Request m_request;

    QLocalSocket *m_socket = nullptr;
    QTimer *m_timer = nullptr;
    /*! Request body file being uploaded (`bodyFile` mode). */
    QFile *m_bodyFile = nullptr;
    /*! Bytes left to write. */
    qint64 m_bodyRemaining = 0;
    /*! Chunk size per write: large enough to cut syscalls, small enough not to stall the event loop. */
    static constexpr qint64 kBodyChunkBytes = 256 * 1024;
    std::unique_ptr<HttpResponseParser> m_parser;

    State m_state = State::Pending;
    int m_httpStatus = 0;
    QByteArray m_body;
    DockerError m_error;
    /*! Total response body seen so far (to detect new data worth signalling). */
    qsizetype m_seenBodyBytes = 0;
    bool m_streamStarted = false;
};

/*!
 * Thin wrapper around the Docker Engine HTTP API (ARCH_V1 §10/§11, ARCH_V4 §2.2.1).
 *
 * Since phase 4 it offers both read-only GET and write requests (POST / DELETE).
 * Write requests may only be issued by `docker_backend.cpp` — asserted by
 * `mutationsHaveSingleChokePoint` in tests/model/tst_source_conventions.cpp, so
 * mutations cannot leak out from anywhere else.
 *
 * All I/O is asynchronous (QLocalSocket + event loop); the UI thread never blocks
 * (§18). The API version prefix is appended here via ApiVersion (§8).
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

    /*! Unversioned request, only for /_ping and /version negotiation. */
    DockerReply *getUnversioned(const QString &apiPath);
    /*! Regular read-only request; adds the v1.xx prefix when the API version is known. */
    DockerReply *get(const QString &apiPath, const QUrlQuery &query = {});
    /*!
     * Write request (no body, Content-Length: 0). `timeoutMs <= 0` uses the client
     * default timeout.
     */
    DockerReply *post(const QString &apiPath,
                      const QUrlQuery &query = {},
                      int timeoutMs = 0,
                      const QMap<QByteArray, QByteArray> &headers = {},
                      const QByteArray &body = {});
    /*!
     * Upload the body from a file (the `POST /build` tar context).
     *
     * `uploadTimeoutMs` is the **write-phase** idle timeout; once the upload is done
     * it switches back to the streaming idle timeout (`idleTimeoutMs`).
     */
    DockerReply *postFile(const QString &apiPath,
                          const QUrlQuery &query,
                          const QString &filePath,
                          const QByteArray &contentType,
                          int uploadTimeoutMs,
                          int idleTimeoutMs,
                          const QMap<QByteArray, QByteArray> &headers = {});
    DockerReply *del(const QString &apiPath, const QUrlQuery &query = {}, int timeoutMs = 0);
    /*!
     * Streaming read request (log follow is a GET).
     *
     * `idleTimeoutMs <= 0` means **no idle timeout**: a `follow=1` log stream may
     * legitimately stay silent for a long time (ARCH_V5_V8 §3.1.1); only cancel or
     * page teardown ends it.
     */
    DockerReply *getStream(const QString &apiPath, const QUrlQuery &query = {}, int idleTimeoutMs = 0);

    /*!
     * Streaming write request: the timeout counts idle time (a pull may legitimately
     * run for a long time).
     */
    DockerReply *postStream(const QString &apiPath, const QUrlQuery &query = {}, int idleTimeoutMs = 0, const QMap<QByteArray, QByteArray> &headers = {});

    /*! Common entry point: path prefixing, timeouts and the streaming flag live here. */
    /*!
     * Builds a request and **starts it immediately**.
     *
     * `bodyFile` / `bodyContentType` / `uploadTimeoutMs` must be passed here:
     * `start()` writes the request to the socket right away, so setting them later
     * is too late (same trap as §5.1).
     */
    DockerReply *request(DockerReply::Method method,
                         const QString &apiPath,
                         const QUrlQuery &query,
                         int timeoutMs,
                         bool streaming,
                         const QMap<QByteArray, QByteArray> &headers = {},
                         const QByteArray &body = {},
                         const QString &bodyFile = {},
                         const QByteArray &bodyContentType = QByteArrayLiteral("application/json"),
                         int uploadTimeoutMs = 0);

private:
    DockerEndpoint m_endpoint = DockerEndpoint::fromEnvironment();
    ApiVersion m_apiVersion;
    int m_timeoutMs = 10000;
};

} // namespace Kontainer
