/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QMetaType>
#include <QString>

namespace Kontainer
{

/*!
 * Layered Docker error (ARCH_V1 §23).
 *
 * One value type carries transport / HTTP / API / JSON failures plus a user-facing
 * translatable text. Errors are never swallowed: every failure must propagate as DockerError.
 */
class DockerError
{
    Q_GADGET

public:
    enum class Kind {
        None, /*!< no error */
        DockerUnavailable, /*!< socket missing / daemon not running */
        ConnectionFailed, /*!< connection refused or dropped */
        PermissionDenied, /*!< socket permission denied, or HTTP 401/403 */
        Timeout, /*!< request timed out */
        ApiVersionMismatch, /*!< no common client/server API version */
        NotFound, /*!< HTTP 404 */
        Conflict, /*!< HTTP 409: state conflict (container running / image in use / name taken) */
        /*! Request never sent: precondition failed (duplicate pull, invalid input, ...). */
        PreconditionFailed,
        HttpError, /*!< other 4xx */
        EngineError, /*!< 5xx */
        InvalidResponse, /*!< not a valid HTTP response */
        UnexpectedPayload, /*!< JSON parse failure or unexpected structure */
    };
    Q_ENUM(Kind)

    DockerError() = default;
    DockerError(Kind kind, QString detail = {}, int httpStatus = 0);

    /*!
     * Kind → stable key (`permissionDenied` / `timeout` / …).
     *
     * The UI picks its next step from the key (e.g. 401/403 offers "Sign in…") rather than
     * matching engine text, which changes across engine versions while keys do not.
     */
    static QString kindKey(Kind kind);

    Kind kind() const
    {
        return m_kind;
    }
    bool isError() const
    {
        return m_kind != Kind::None;
    }

    /*! Technical detail for logs/debugging; never translated, never holds secrets like raw env values. */
    QString detail() const
    {
        return m_detail;
    }
    int httpStatus() const
    {
        return m_httpStatus;
    }

    /*!
     * For user-visible text use dockerErrorText() in the presentation layer
     * (model/docker_error_text.h): the backend produces no UI text (ARCH_V1 §6.3).
     */

    /*! HTTP status → error kind (ARCH_V1 §28 Error mapping). */
    static DockerError fromHttpStatus(int status, const QString &apiMessage = {});

    /*! QAbstractSocket::SocketError value → error kind. */
    static DockerError fromSocketError(int socketError, const QString &socketErrorString);

private:
    Kind m_kind = Kind::None;
    QString m_detail;
    int m_httpStatus = 0;
};

} // namespace Kontainer

Q_DECLARE_METATYPE(Kontainer::DockerError)
