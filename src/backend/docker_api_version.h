/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QString>

#include <optional>

namespace Kontainer
{

/*!
 * Docker Engine API 版本策略（ARCH_V1 §8）。
 *
 * 这是项目中唯一允许出现 "v1.xx" 的地方：任何其他源文件都不得散落版本字符串，
 * 而是通过本类型获取路径前缀。
 *
 * 协商流程：GET /version → 取服务端 ApiVersion 与 MinAPIVersion →
 * 在客户端支持区间内取 min(服务端 ApiVersion, 客户端上限)。
 */
class ApiVersion
{
public:
    constexpr ApiVersion() = default;
    constexpr ApiVersion(int major, int minor)
        : m_major(major)
        , m_minor(minor)
    {
    }

    /*! 客户端支持的最低 Engine API 版本（Docker 20.10 起）。 */
    static constexpr int clientMinMinor()
    {
        return 41;
    }
    /*! 客户端支持的最高 Engine API 版本（Docker 29.x）。 */
    static constexpr int clientMaxMinor()
    {
        return 56;
    }

    /*! 解析 "1.56" 这类版本字符串；非法输入返回 nullopt。 */
    static std::optional<ApiVersion> fromString(const QString &version);

    /*!
     * 在客户端支持区间内协商出一个双方都支持的版本。
     * 服务端过旧（低于客户端下限）或过新（最低要求高于客户端上限）时返回 nullopt，
     * 调用方应转成 DockerError::Kind::ApiVersionMismatch。
     */
    static std::optional<ApiVersion> negotiate(const ApiVersion &server, const std::optional<ApiVersion> &serverMin);

    bool isValid() const
    {
        return m_major > 0;
    }
    int major() const
    {
        return m_major;
    }
    int minor() const
    {
        return m_minor;
    }

    QString toString() const;
    /*! 请求路径前缀，例如 "v1.56"。 */
    QString pathPrefix() const;

    friend bool operator==(const ApiVersion &lhs, const ApiVersion &rhs)
    {
        return lhs.m_major == rhs.m_major && lhs.m_minor == rhs.m_minor;
    }
    friend bool operator<(const ApiVersion &lhs, const ApiVersion &rhs)
    {
        return lhs.m_major != rhs.m_major ? lhs.m_major < rhs.m_major : lhs.m_minor < rhs.m_minor;
    }
    friend bool operator>(const ApiVersion &lhs, const ApiVersion &rhs)
    {
        return rhs < lhs;
    }

private:
    int m_major = 0;
    int m_minor = 0;
};

} // namespace Kontainer
