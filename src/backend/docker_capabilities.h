/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/docker_endpoint.h"

#include <QString>

namespace Kontainer
{

/*!
 * 写权限门（ARCH_V4 §2.2.3）。
 *
 * 权限模型（ARCH_V3 §1.3 已定）：**按 socket 实际权限工作，不引入提权机制**。
 * 本类只做一次便宜的判定，不做任何 I/O 之外的事情：
 *  - 端点必须是本机 unix socket（远程 endpoint 一律只读，不变式）
 *  - socket 文件必须存在
 *  - socket 必须对当前进程可写（QFileInfo::isWritable() 在 Unix 上就是 access(2) 的结果）
 *
 * 判定为不可写时 UI 不出现写入口；即便如此，真正的权威仍然是引擎：
 * 任何一次 mutation 返回 403 / EACCES 都会让会话降级为只读（OperationController）。
 */
enum class WriteAccess {
    Allowed,
    /*! socket 不存在：引擎可能没跑。 */
    SocketMissing,
    /*! socket 不可写：当前用户不在 socket 所属组，或权限被收紧。 */
    SocketNotWritable,
    /*! 非本机 unix socket（远程 endpoint）：按设计只读。 */
    UnsupportedEndpoint,
};

/*! 稳定 key：allowed / denied / unsupported（QML 据此决定要不要渲染写入口）。 */
QString writeAccessKey(WriteAccess access);
/*! 是否允许出现写入口。 */
inline bool writeAccessAllowed(WriteAccess access)
{
    return access == WriteAccess::Allowed;
}

WriteAccess writeAccessFor(const DockerEndpoint &endpoint);

} // namespace Kontainer
