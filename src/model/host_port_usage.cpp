/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/host_port_usage.h"

#include "model/port_binding_rules.h"

#include <algorithm>

#include <QSet>

#include <QtGlobal>

namespace Kontainer
{

namespace
{
using namespace PortBindingRules;

/*! 只有跑着的容器才真的占着宿主端口（用户已确认：没运行的自然不占用）。 */
bool holdsHostPorts(ContainerState state)
{
    switch (state) {
    case ContainerState::Running:
    case ContainerState::Paused:
    case ContainerState::Restarting:
        return true;
    default:
        return false;
    }
}

/*! 建议端口的起点：低于 1024 需要特权，不适合作为建议。 */
constexpr int kFirstSuggestedPort = 8000;
} // namespace

QString HostPortEntry::portText() const
{
    if (hostPortEnd != 0 && hostPortEnd != hostPort) {
        return QStringLiteral("%1-%2").arg(hostPort).arg(hostPortEnd);
    }
    return QString::number(hostPort);
}

QString HostPortEntry::displayAddress() const
{
    if (dualStack || hostIp.isEmpty() || isWildcardAddress(hostIp)) {
        // 所有接口：地址由图形（双环）或"所有接口"语义表达，文字只留端口
        return portText();
    }
    return QStringLiteral("%1:%2").arg(hostIp, portText());
}

QList<HostPortEntry> HostPortUsage::entriesFor(const QList<Container> &containers,
                                               const QHash<QString, QList<DeclaredPortBinding>> &declared)
{
    QList<HostPortEntry> entries;
    for (const Container &container : containers) {
        if (!holdsHostPorts(container.state)) {
            continue;
        }
        for (const Port &port : container.ports) {
            if (!port.isPublished()) {
                continue;
            }
            const QString hostIp = port.ip;
            /*
             * IPv4/IPv6 通配合并：同容器 + 同容器端口 + 同协议 + 同宿主端口，
             * 一条 IPv4 通配一条 IPv6 通配 → 合成一条 dualStack（与端口拓扑页同一规则）。
             */
            bool merged = false;
            for (HostPortEntry &existing : entries) {
                if (existing.containerId == container.id && existing.containerPort == port.privatePort
                    && existing.protocol == (port.type.isEmpty() ? QStringLiteral("tcp") : port.type)
                    && existing.hostPort == port.publicPort && !existing.dualStack && isWildcardAddress(existing.hostIp)
                    && isWildcardAddress(hostIp) && wildcardFamily(existing.hostIp) != wildcardFamily(hostIp)) {
                    existing.dualStack = true;
                    existing.ipv4 = true;
                    existing.ipv6 = true;
                    existing.hostIp.clear();
                    merged = true;
                    break;
                }
            }
            if (merged) {
                continue;
            }

            HostPortEntry entry;
            entry.hostPort = port.publicPort;
            entry.hostPortEnd = port.publicPort;
            entry.hostIp = isWildcardAddress(hostIp) ? QString() : hostIp;
            entry.ipv4 = wildcardFamily(hostIp) != 6;
            entry.ipv6 = wildcardFamily(hostIp) == 6;
            entry.dualStack = false;
            entry.containerPort = port.privatePort;
            entry.protocol = port.type.isEmpty() ? QStringLiteral("tcp") : port.type;
            entry.stateKey = QStringLiteral("inUse");
            entry.containerId = container.id;
            entry.containerName = container.name;
            entry.containerImage = container.image;
            entry.containerStateKey = container.stateKey();
            entries.append(entry);
        }
    }

    /*
     * 声明的绑定：已经真的发布了的不再重复出现（发布那条就是它），
     * 剩下的就是"声明了但没生效"——端口页要如实标出来（决定 3）。
     */
    for (const Container &container : containers) {
        /*
         * 这里**故意**不过滤运行状态（与 `holderFor()` 相反）：
         *  - 运行中容器声明了却没发布的 → `declaredNotPublished`（"占着却连不上"）；
         *  - 未运行容器声明过的 → `reserved`："端口现在是空的，但那个容器一起来就会要回去"。
         * 用户实测反馈：这两种都要能在端口页看到（早前决定"不做 reserved"已被这次反馈推翻）。
         * 冲突判断（`holderFor`）仍然只算运行中的容器——没运行就不该拦住别人。
         */
        const QList<DeclaredPortBinding> bindings = declared.value(container.id);
        for (const DeclaredPortBinding &binding : bindings) {
            const bool published = std::any_of(entries.cbegin(), entries.cend(), [&](const HostPortEntry &entry) {
                return entry.containerId == container.id && entry.containerPort == binding.containerPort
                    && entry.protocol == binding.protocol && entry.hostPort == binding.hostPort
                    && hostBindingsOverlap(entry.hostIp, binding.hostIp);
            });
            if (published) {
                continue;
            }
            HostPortEntry entry;
            entry.hostPort = binding.hostPort;
            entry.hostPortEnd = binding.hostPortEnd != 0 ? binding.hostPortEnd : binding.hostPort;
            entry.hostIp = isWildcardAddress(binding.hostIp) ? QString() : binding.hostIp;
            entry.ipv4 = wildcardFamily(binding.hostIp) != 6;
            entry.ipv6 = wildcardFamily(binding.hostIp) == 6;
            entry.containerPort = binding.containerPort;
            entry.protocol = binding.protocol;
            if (holdsHostPorts(container.state)) {
                entry.stateKey = QStringLiteral("declaredNotPublished");
            } else {
                /*
                 * 没在运行的容器：端口声明还在，但**这个端口现在被别的容器占着**时
                 * 它一起来就会因为端口冲突失败（用户要求标红"被占用"）。
                 */
                const bool taken = std::any_of(entries.cbegin(), entries.cend(), [&](const HostPortEntry &other) {
                    if (other.stateKey != QLatin1String("inUse") || other.containerId == container.id) {
                        return false;
                    }
                    const quint16 otherLast = other.hostPortEnd != 0 ? other.hostPortEnd : other.hostPort;
                    const quint16 declaredLast = binding.hostPortEnd != 0 ? binding.hostPortEnd : binding.hostPort;
                    return other.hostPort <= declaredLast && otherLast >= binding.hostPort
                        && hostBindingsOverlap(other.hostIp, binding.hostIp);
                });
                entry.stateKey = taken ? QStringLiteral("reservedTaken") : QStringLiteral("reserved");
            }
            entry.containerId = container.id;
            entry.containerName = container.name;
            entry.containerImage = container.image;
            entry.containerStateKey = container.stateKey();
            entries.append(entry);
        }
    }

    // 顺序稳定：端口 → 容器名（刷新时行不会跳）
    std::sort(entries.begin(), entries.end(), [](const HostPortEntry &lhs, const HostPortEntry &rhs) {
        if (lhs.hostPort != rhs.hostPort) {
            return lhs.hostPort < rhs.hostPort;
        }
        if (lhs.containerName != rhs.containerName) {
            return lhs.containerName < rhs.containerName;
        }
        return lhs.containerPort < rhs.containerPort;
    });
    return entries;
}

QString HostPortUsage::holderFor(const QList<Container> &containers, const QString &hostIp, int hostPort)
{
    if (hostPort <= 0) {
        return {}; // 0 = 随机分配，不冲突
    }
    for (const Container &container : containers) {
        if (!holdsHostPorts(container.state)) {
            continue;
        }
        for (const Port &port : container.ports) {
            if (port.isPublished() && port.publicPort == hostPort && hostBindingsOverlap(port.ip, hostIp)) {
                return container.name.isEmpty() ? container.shortId() : container.name;
            }
        }
    }
    return {};
}

int HostPortUsage::nextFreePort(const QList<Container> &containers, int afterPort, const QList<int> &extraUsed)
{
    QList<int> used;
    for (const HostPortEntry &entry : entriesFor(containers)) {
        used.append(entry.hostPort);
    }
    for (const int port : extraUsed) {
        if (port > 0) {
            used.append(port);
        }
    }
    const int start = afterPort > 0 ? afterPort + 1 : kFirstSuggestedPort;
    for (int candidate = std::max(start, kFirstSuggestedPort); candidate <= 65535; ++candidate) {
        if (!used.contains(candidate)) {
            return candidate;
        }
    }
    return 0;
}

QString HostPortUsage::stateKeyForPort(const QList<HostPortEntry> &entries, quint16 port)
{
    for (const HostPortEntry &entry : entries) {
        const quint16 last = entry.hostPortEnd != 0 ? entry.hostPortEnd : entry.hostPort;
        if (port >= entry.hostPort && port <= last) {
            return entry.stateKey;
        }
    }
    return {};
}

QList<HostPortRange> HostPortUsage::clusterRanges(const QList<HostPortEntry> &entries, int gap, int margin, int tilesPerRange)
{
    QList<HostPortRange> ranges;
    if (entries.isEmpty()) {
        return ranges;
    }

    // 已占用的端口（升序、去重）。用 QSet 去重：原来对 QList 调 contains() 是 O(n)，
    // 端口多的容器（几十上百个区间端口）会变成平方级，切换筛选时肉眼可见地卡。
    QSet<quint16> seen;
    QList<quint16> used;
    for (const HostPortEntry &entry : entries) {
        const quint16 last = entry.hostPortEnd != 0 ? entry.hostPortEnd : entry.hostPort;
        for (quint16 port = entry.hostPort; port <= last; ++port) {
            if (!seen.contains(port)) {
                seen.insert(port);
                used.append(port);
            }
        }
    }
    std::sort(used.begin(), used.end());

    const int safeGap = qMax(0, gap);
    const int safeMargin = qMax(0, margin);
    const int safeTiles = qMax(1, tilesPerRange);

    int index = 0;
    while (index < used.size()) {
        quint16 first = used.at(index);
        quint16 last = first;
        int cursor = index;
        while (cursor + 1 < used.size() && int(used.at(cursor + 1)) - int(used.at(cursor)) <= safeGap + 1) {
            last = used.at(cursor + 1);
            ++cursor;
        }
        // 向两侧扩展几个空闲端口：让用户看到"这一段附近哪里空着"
        const int expandedFirst = qMax(1, int(first) - safeMargin);
        const int expandedLast = qMin(65535, int(last) + safeMargin);
        first = quint16(expandedFirst);
        last = quint16(expandedLast);

        HostPortRange range;
        range.first = first;
        range.last = last;
        const int total = int(last) - int(first) + 1;
        range.tileCount = qMin(total, safeTiles);
        range.hiddenCount = qMax(0, total - safeTiles);
        /*
         * `usedCount` 数的是**端口个数**（去重），不是"声明了几条"。
         *
         * 用户实测：`WinBoat` 声明了 5 段（每段 10 个）落在同一区间里，
         * 其中两段还重叠，原来显示"5 个端口被占用"——而图上明明亮着几十个格子。
         */
        QSet<quint16> covered;
        for (const HostPortEntry &entry : entries) {
            const quint16 entryLast = entry.hostPortEnd != 0 ? entry.hostPortEnd : entry.hostPort;
            const quint16 from = qMax(entry.hostPort, first);
            const quint16 to = qMin(entryLast, last);
            for (quint16 port = from; port <= to; ++port) {
                covered.insert(port);
            }
        }
        range.usedCount = int(covered.size());
        ranges.append(range);
        index = cursor + 1;
    }
    return ranges;
}

} // namespace Kontainer
