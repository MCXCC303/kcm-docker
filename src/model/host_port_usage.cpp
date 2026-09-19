/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/host_port_usage.h"

#include "model/port_binding_rules.h"

#include <algorithm>

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

QList<HostPortEntry> HostPortUsage::entriesFor(const QList<Container> &containers)
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

} // namespace Kontainer
