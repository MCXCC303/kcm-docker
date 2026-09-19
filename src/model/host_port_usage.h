/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "domain/container.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace Kontainer
{

/*!
 * 一条宿主端口占用（ARCH_next_ports.md §2/§3）。
 *
 * 数据只来自**容器列表**（`/containers/json` 的 `Ports`，即"实际发布"的绑定）——
 * 本期不做 `reserved`（已停止容器声明过什么），因此不读 inspect。
 */
struct HostPortEntry {
    /*! 宿主端口（区间时是起点）。 */
    quint16 hostPort = 0;
    /*! 区间终点；单端口时等于 `hostPort`。 */
    quint16 hostPortEnd = 0;
    /*! IPv4 通配可用（`0.0.0.0` 或具体 IPv4 地址）。 */
    bool ipv4 = false;
    /*! IPv6 通配可用（`::`）。 */
    bool ipv6 = false;
    /*!
     * 这一条同时代表 IPv4 与 IPv6 的通配绑定。
     *
     * Docker 对"没指定宿主地址"的映射会同时建 `0.0.0.0:<port>` 与 `[::]:<port>`；
     * 界面（端口页、端口拓扑）都把它们显示成**一条**，并用双环表示两种协议栈。
     */
    bool dualStack = false;
    /*! 具体绑定地址（通配时为空；用于展示 `127.0.0.1:8100` 这种）。 */
    QString hostIp;
    /*! 容器侧端口与协议。 */
    quint16 containerPort = 0;
    QString protocol;
    /*! `inUse`（运行中的容器真的发布了它）/ `declaredNotPublished`（本期不产生）。 */
    QString stateKey;
    /*! 占用者。 */
    QString containerId;
    QString containerName;
    QString containerImage;
    /*! 容器状态 key（界面按它决定"跳转 / 停止"等动作）。 */
    QString containerStateKey;

    /*! 展示用的一行地址：`20004`（所有接口）/ `127.0.0.1:8100` / `47300-47309`。 */
    QString displayAddress() const;
    /*! 端口文本（区间写成 `47300-47309`）。 */
    QString portText() const;

    friend bool operator==(const HostPortEntry &lhs, const HostPortEntry &rhs)
    {
        return lhs.hostPort == rhs.hostPort && lhs.hostPortEnd == rhs.hostPortEnd && lhs.ipv4 == rhs.ipv4 && lhs.ipv6 == rhs.ipv6
            && lhs.dualStack == rhs.dualStack && lhs.hostIp == rhs.hostIp && lhs.containerPort == rhs.containerPort
            && lhs.protocol == rhs.protocol && lhs.stateKey == rhs.stateKey && lhs.containerId == rhs.containerId;
    }
};

/*!
 * 宿主端口占用表（ARCH_next_ports.md §3 的 `HostPortUsage`）。
 *
 * 纯函数式的门面：输入容器列表，输出"哪个宿主端口被谁占着"。
 * 端口页（M3）、区间地图（M4）与创建表单的冲突检测（M2）都从这里取数——
 * 不要再各自实现一遍（`hostBindingsOverlap` 曾经就有两份，见 `PortBindingRules`）。
 */
class HostPortUsage
{
public:
    /*!
     * 从容器列表构造占用表。
     *
     * 规则：
     *   - 只算**跑着的**容器（`Running` / `Paused` / `Restarting`）：没运行的容器不持有宿主端口，
     *     把它们的声明算作占用会挡住其它应用（用户已确认）；
     *   - IPv4 与 IPv6 通配的同端口绑定合并成一条并标记 `dualStack`；
     *   - 结果按宿主端口升序，同一端口按容器名排序（刷新时行不会跳）。
     */
    static QList<HostPortEntry> entriesFor(const QList<Container> &containers);

    /*!
     * 占用该宿主端口的容器名（没有则空）。
     *
     * `hostIp` 为空/通配时与任何绑定都算冲突（见 `PortBindingRules::hostBindingsOverlap`）。
     */
    static QString holderFor(const QList<Container> &containers, const QString &hostIp, int hostPort);

    /*!
     * `afterPort` 之后的第一个空闲宿主端口（用于"建议端口"；找不到时返回 0）。
     *
     * 只在**已知被占用**的端口之外找，且不超过 65535；`afterPort <= 0` 时从 8000 起找
     * （低于 1024 需要特权，不作为建议）。
     *
     * `extraUsed` 用于"正在编辑的这张表单"：同一请求里其它行已经填了的宿主端口也要避开，
     * 否则建议出来的端口会在提交时因为"请求内重复"被自己拦下。
     */
    static int nextFreePort(const QList<Container> &containers, int afterPort, const QList<int> &extraUsed = {});
};

} // namespace Kontainer
