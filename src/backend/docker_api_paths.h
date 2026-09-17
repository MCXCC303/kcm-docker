/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QString>

namespace Kontainer::ApiPaths
{

/*!
 * 全部 Docker Engine REST 路径的唯一构造点（ARCH_V4 §2.2.1）。
 *
 * 为什么单独一个头文件：ARCH_V4 §1.5 用测试断言「REST 路径片段只允许出现在这里」，
 * 这样新增一个端点必须经过一次被审阅的改动，而不是在某个 .cpp 里顺手拼一个字符串。
 * 注意与 `docker_endpoint.h` 的区别：那个文件描述**连接端点**（socket / DOCKER_HOST），
 * 这里描述**请求路径**，两者不混。
 *
 * 版本前缀（`/v1.xx`）由 DockerClient 拼接，这里只写版本无关的路径。
 */

/* --- 未版本化：仅版本协商使用 --- */
inline QString ping()
{
    return QStringLiteral("/_ping");
}
inline QString version()
{
    return QStringLiteral("/version");
}

/* --- 只读端点 --- */
inline QString info()
{
    return QStringLiteral("/info");
}
inline QString containersJson()
{
    return QStringLiteral("/containers/json");
}
inline QString imagesJson()
{
    return QStringLiteral("/images/json");
}
inline QString systemDf()
{
    return QStringLiteral("/system/df");
}
inline QString containerInspect(const QString &id)
{
    return QStringLiteral("/containers/%1/json").arg(id);
}
inline QString imageInspect(const QString &id)
{
    return QStringLiteral("/images/%1/json").arg(id);
}
inline QString containerStats(const QString &id)
{
    return QStringLiteral("/containers/%1/stats").arg(id);
}

/* --- 写端点（ARCH_V4 §2.2 / 附录 A.5） --- */
inline QString containerStart(const QString &id)
{
    return QStringLiteral("/containers/%1/start").arg(id);
}
inline QString containerStop(const QString &id)
{
    return QStringLiteral("/containers/%1/stop").arg(id);
}
inline QString containerRestart(const QString &id)
{
    return QStringLiteral("/containers/%1/restart").arg(id);
}
inline QString containerRemove(const QString &id)
{
    return QStringLiteral("/containers/%1").arg(id);
}
inline QString imageCreate()
{
    return QStringLiteral("/images/create");
}
/*!
 * 校验仓库凭据（`POST /auth`，ARCH_V5_V8 §2.6）。
 *
 * 凭据走 `X-Registry-Auth` 头：本机 API 1.56 实测，请求畸形时引擎回的是
 * `invalid X-Registry-Auth header: …`，说明它读的是头（而不是 body）。
 * 因此这里既不带 query 也不带请求体——凭据绝不出现在 URL 里。
 */
inline QString auth()
{
    return QStringLiteral("/auth");
}

/*!
 * 删除镜像。
 *
 * `name` 可以是 `<repo>:<tag>`（只删该标签）或镜像 ID（删整个镜像，多标签时引擎返回 409）。
 * 仓库名里可能含 `/`（例如 `registry:5000/team/app:1.0`），这里**不做百分号编码**：
 * Docker 的路由把该段的剩余部分整体当作 name 处理，编码反而会让路由匹配失败。
 */
inline QString imageRemove(const QString &name)
{
    return QStringLiteral("/images/%1").arg(name);
}

/*!
 * 四期引入的 mutation。
 *
 * 每个 mutation 声明自己需要的最低 Engine API 版本（1.x 的 x 部分）：
 * 版本不满足时必须给出明确提示，而不是让引擎返回一个难懂的错误（ARCH_V3_pre §2.2）。
 * 目前全部 ≤ 24，远低于客户端下限 41，因此运行时不会触发；
 * 测试会断言这一点，未来新增需要更高版本的端点时，门会自然出现。
 */
enum class Mutation {
    StartContainer,
    StopContainer,
    RestartContainer,
    RemoveContainer,
    PullImage,
    RemoveImage,
};

constexpr int requiredApiMinor(Mutation mutation)
{
    switch (mutation) {
    case Mutation::StartContainer:
    case Mutation::StopContainer:
    case Mutation::RestartContainer:
    case Mutation::RemoveContainer:
    case Mutation::PullImage:
    case Mutation::RemoveImage:
        return 24; // 这几个端点自 API 1.24 起语义稳定
    }
    return 24;
}

} // namespace Kontainer::ApiPaths
