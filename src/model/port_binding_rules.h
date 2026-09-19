/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QString>

namespace Kontainer
{

/*!
 * 宿主端口绑定的共享规则（ARCH_next_ports.md §3）。
 *
 * 这些判断原来散在两个地方（`OperationController` 与 `CreateContainerController`
 * 各有一份"地址是否重叠"的实现），端口管理要同时服务创建表单、端口页与端口拓扑，
 * 因此先收成一份**纯函数**，谁都不许再抄第二遍。
 */
namespace PortBindingRules
{

/*!
 * 通配地址：空串 / `0.0.0.0`（IPv4 所有接口）/ `::` 或 `[::]`（IPv6 所有接口）。
 *
 * Docker 对"没指定宿主地址"的映射会同时建 IPv4 与 IPv6 两条，两条都是通配。
 */
bool isWildcardAddress(const QString &hostIp);

/*! 通配地址属于哪一族：4 = IPv4，6 = IPv6，0 = 不是通配。 */
int wildcardFamily(const QString &hostIp);

/*!
 * 两个宿主绑定地址是否有交集（同一端口上是否互斥）。
 *
 * 通配与任何地址都冲突（`0.0.0.0:8100` 与 `[::]:8100` 在 Linux 默认也互斥，
 * 除非 `net.ipv6.bindv6only=1`——保守起见一律算冲突）；
 * 两个具体地址只有**完全相同**才冲突（`127.0.0.1:8100` 与 `192.168.1.5:8100` 可以共存）。
 */
bool hostBindingsOverlap(const QString &lhs, const QString &rhs);

/*!
 * 解析 `HostConfig.PortBindings` 里的 `HostPort`：可以是单个端口，也可以是区间。
 *
 * 实测 `WinBoat` 用的是 `"47300-47309"` 这种区间写法，因此端口模型必须支持区间。
 *
 * @param first 区间起点（解析失败时不变）
 * @param last  区间终点（单端口时等于 `first`）
 * @return 解析成功
 */
bool parseHostPortSpec(const QString &spec, quint16 *first, quint16 *last);

} // namespace PortBindingRules

} // namespace Kontainer
