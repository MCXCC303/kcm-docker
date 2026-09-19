/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "domain/container.h"
#include "domain/container_detail.h"

#include <QHash>
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
        // 注意：**展示用的字段也要比**（容器名/镜像）——漏掉它们时改名或换镜像不会
        // 触发 dataChanged，页面就会一直显示旧值（用例 refreshKeepsTheModelIntact 守住这一点）
        return lhs.hostPort == rhs.hostPort && lhs.hostPortEnd == rhs.hostPortEnd && lhs.ipv4 == rhs.ipv4 && lhs.ipv6 == rhs.ipv6
            && lhs.dualStack == rhs.dualStack && lhs.hostIp == rhs.hostIp && lhs.containerPort == rhs.containerPort
            && lhs.protocol == rhs.protocol && lhs.stateKey == rhs.stateKey && lhs.containerId == rhs.containerId
            && lhs.containerName == rhs.containerName && lhs.containerImage == rhs.containerImage
            && lhs.containerStateKey == rhs.containerStateKey;
    }
};

/*!
 * 区间地图里的一段（ARCH_next_ports.md §4.B，里程碑 M4）。
 *
 * 端口分布通常集中在几段（例如 8000-8010、20001-20004），把 0-65535 全画出来
 * 只会让人看到一片空白；因此按"相邻已用端口的间隔"聚类成若干段再画。
 */
struct HostPortRange {
    /*! 段的范围（含两端；`last - first + 1` 是这一段覆盖的端口数）。 */
    quint16 first = 0;
    quint16 last = 0;
    /*! 这一段实际渲染的方块数（受 `tilesPerRange` 限制）。 */
    int tileCount = 0;
    /*! 因为上限而没渲染出来的端口数（> 0 时界面显示"还有 N 个"）。 */
    int hiddenCount = 0;
    /*! 这一段里有几个端口被容器占着（标题上给个摘要）。 */
    int usedCount = 0;

    friend bool operator==(const HostPortRange &lhs, const HostPortRange &rhs)
    {
        return lhs.first == rhs.first && lhs.last == rhs.last && lhs.tileCount == rhs.tileCount
            && lhs.hiddenCount == rhs.hiddenCount && lhs.usedCount == rhs.usedCount;
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
    /*!
     * 端口页用：把"实际发布"（容器列表）与"声明"（inspect，只对**运行中**的容器取）
     * 合到一张表里（用户拍板决定 3：按"声明 vs 实际发布"的语义）。
     *
     * - 声明且**真的发布了** → 一条 `inUse`（以发布为准，地址/端口取实际的）
     * - 声明了但**没有发布**（容器在跑） → 一条 `declaredNotPublished`
     * - 容器没在运行、端口现在是空的 → 一条 `reserved`
     * - 容器没在运行、但端口**已经被别的容器占着** → 一条 `reservedTaken`
     *   （用户要求：这种要标红"被占用"——它启动时会因为端口冲突直接失败）
     * - 只发布没声明（理论上不该有） → 仍然按 `inUse` 收进来
     *
     * `declared` 的键是容器 id；缺省（空）时行为与单参数版本完全一致。
     */
    static QList<HostPortEntry> entriesFor(const QList<Container> &containers,
                                           const QHash<QString, QList<DeclaredPortBinding>> &declared = {});

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

    /*!
     * 把占用表聚类成区间（区间地图用）。
     *
     * 规则：
     *  - 端口升序后，相邻**已占用**端口的间隔 ≤ `gap` 就归为同一段；
     *  - 每段向两侧各扩展 `margin` 个端口（让用户看到"附近哪里还空着"）；
     *  - 每段渲染的方块数不超过 `tilesPerRange`，超出的部分记进 `hiddenCount`
     *    （界面显示"还有 N 个"）——没有这个上限，1000-1100 这种区间会拖垮界面。
     */
    static QList<HostPortRange> clusterRanges(const QList<HostPortEntry> &entries, int gap = 5, int margin = 2,
                                             int tilesPerRange = 64);

    /*!
     * 某个端口在占用表里的状态 key（区间地图的方块按它上色）。
     *
     * - 区间（`47300-47309`）按整段算——落在区间里也算被占；
     * - 同一个端口上可能有多条（例如 20003 既"被运行中的容器占着"，又被某个未运行的容器声明过）：
     *   按**优先级**取 `inUse > reservedTaken > reserved > declaredNotPublished`，
     *   因此"全部端口"视图里运行中永远压过被占用/未占用（用户要求）；
     * - `preferred` 非空时优先取其中的状态：地图切到某个筛选时，只有该状态的方块才该显示出来。
     */
    static QString stateKeyForPort(const QList<HostPortEntry> &entries, quint16 port,
                                   const QStringList &preferred = {});

    /*! 该端口上占用它的容器（用于地图里点击跳转；没有则空）。 */
    static HostPortEntry entryForPort(const QList<HostPortEntry> &entries, quint16 port,
                                      const QStringList &preferred = {});
};

} // namespace Kontainer
