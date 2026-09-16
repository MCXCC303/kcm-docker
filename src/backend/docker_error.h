/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QMetaType>
#include <QString>

namespace Kontainer
{

/*!
 * 分层的 Docker 错误（ARCH_V1 §23）。
 *
 * Transport / HTTP / API / JSON 各层错误都由这一个值类型表达，并附带一个
 * 面向用户的可翻译文本。绝不吞掉错误：任何失败都必须能以 DockerError 传播。
 */
class DockerError
{
    Q_GADGET

public:
    enum class Kind {
        None, /*!< 没有错误 */
        DockerUnavailable, /*!< socket 不存在 / daemon 未运行 */
        ConnectionFailed, /*!< 连接被拒绝或中断 */
        PermissionDenied, /*!< socket 权限不足，或 HTTP 401/403 */
        Timeout, /*!< 请求超时 */
        ApiVersionMismatch, /*!< 客户端与服务端 API 版本无交集 */
        NotFound, /*!< HTTP 404 */
        HttpError, /*!< 其他 4xx */
        EngineError, /*!< 5xx */
        InvalidResponse, /*!< 不是合法 HTTP 响应 */
        UnexpectedPayload, /*!< JSON 解析失败或结构与预期不符 */
    };
    Q_ENUM(Kind)

    DockerError() = default;
    DockerError(Kind kind, QString detail = {}, int httpStatus = 0);

    Kind kind() const
    {
        return m_kind;
    }
    bool isError() const
    {
        return m_kind != Kind::None;
    }

    /*! 技术细节（供日志/调试；不可翻译，且不得包含环境变量原值等敏感内容）。 */
    QString detail() const
    {
        return m_detail;
    }
    int httpStatus() const
    {
        return m_httpStatus;
    }

    /*!
     * 用户可见文本请使用 presentation layer 的 dockerErrorText()（见
     * model/docker_error_text.h）：backend 不产出 UI 文案（ARCH_V1 §6.3）。
     */

    /*! HTTP 状态码 → 错误分类（ARCH_V1 §28 Error mapping）。 */
    static DockerError fromHttpStatus(int status, const QString &apiMessage = {});

    /*! QAbstractSocket::SocketError 数值 → 错误分类。 */
    static DockerError fromSocketError(int socketError, const QString &socketErrorString);

private:
    Kind m_kind = Kind::None;
    QString m_detail;
    int m_httpStatus = 0;
};

} // namespace Kontainer

Q_DECLARE_METATYPE(Kontainer::DockerError)
