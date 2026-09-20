/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
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

/*! Only running containers really hold host ports (user-confirmed: stopped ones do not occupy ports). */
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

/*! Start of suggested ports: below 1024 needs privileges, so it is unsuitable as a suggestion. */
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
        // All interfaces: the graphic (double ring) conveys the address, so the text keeps only the port
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
             * Merge IPv4/IPv6 wildcards: same container + container port + protocol + host port, one IPv4
             * wildcard and one IPv6 wildcard -> one dualStack row (same rule as the port topology page).
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
     * Declared bindings: those already really published are not repeated (the published row is them);
     * what remains is "declared but not in effect" -- the ports page must report it (decision 3).
     */
    for (const Container &container : containers) {
        /*
         * Run state is **deliberately** not filtered here (unlike `holderFor()`):
         *  - running container, declared but not publishing -> `declaredNotPublished` (held but unreachable);
         *  - stopped container's declaration -> `reserved`: "the port is free now, but that container takes
         *    it back as soon as it starts".
         * User feedback: both must be visible on the ports page (the earlier "no reserved" decision was
         * overturned by it). Conflict checks (`holderFor`) still count running containers only -- a
         * stopped one must not block others.
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
                 * Stopped container: its declaration stands, but if the port is **currently held by
                 * another container** it will fail on start with a port conflict (user wants it marked red).
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

    // Stable order: port, then container name (rows do not jump on refresh)
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
        return {}; // 0 = random assignment, so no conflict
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

namespace
{
/*!
 * Whether a filter key "selects" an entry state.
 *
 * The `reserved` bucket covers both `reserved` (not started) and `reservedTaken` (taken), matching
 * `HostPortFilterModel`; otherwise, with the map on "not started / taken", taken tiles would still be
 * shown as running by priority.
 */
bool preferredMatches(const QString &stateKey, const QStringList &preferred)
{
    if (preferred.contains(stateKey)) {
        return true;
    }
    return preferred.contains(QLatin1String("reserved"))
        && (stateKey == QLatin1String("reserved") || stateKey == QLatin1String("reservedTaken"));
}

/*! State priority: in use > taken > not started > free (tile color in the "all ports" view). */
int statePriority(const QString &stateKey)
{
    if (stateKey == QLatin1String("inUse")) {
        return 4;
    }
    if (stateKey == QLatin1String("reservedTaken")) {
        return 3;
    }
    if (stateKey == QLatin1String("reserved")) {
        return 2;
    }
    if (stateKey == QLatin1String("declaredNotPublished")) {
        return 1;
    }
    return 0;
}
} // namespace

HostPortEntry HostPortUsage::entryForPort(const QList<HostPortEntry> &entries, quint16 port, const QStringList &preferred)
{
    HostPortEntry best;
    int bestPriority = -1;
    for (const HostPortEntry &entry : entries) {
        const quint16 last = entry.hostPortEnd != 0 ? entry.hostPortEnd : entry.hostPort;
        if (port < entry.hostPort || port > last) {
            continue;
        }
        // With a filter, favor the states it selects; otherwise use the fixed priority
        const int priority = preferred.isEmpty()
            ? statePriority(entry.stateKey)
            : (preferredMatches(entry.stateKey, preferred) ? 100 + statePriority(entry.stateKey)
                                                           : statePriority(entry.stateKey));
        if (priority > bestPriority) {
            bestPriority = priority;
            best = entry;
        }
    }
    return best;
}

QString HostPortUsage::stateKeyForPort(const QList<HostPortEntry> &entries, quint16 port, const QStringList &preferred)
{
    return entryForPort(entries, port, preferred).stateKey;
}

QList<HostPortRange> HostPortUsage::clusterRanges(const QList<HostPortEntry> &entries, int gap, int margin, int tilesPerRange)
{
    QList<HostPortRange> ranges;
    if (entries.isEmpty()) {
        return ranges;
    }

    // Used ports (ascending, deduplicated). QSet avoids the old O(n) QList::contains(), which went
    // quadratic for containers with dozens of range ports and visibly stalled filter switches.
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
        // Expand a few free ports on both sides so users see where it is free around this segment
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
         * `usedCount` counts **ports** (deduplicated), not declarations.
         *
         * User report: `WinBoat` declared 5 ranges of 10 ports inside one segment, two of them
         * overlapping, and the old code said "5 ports used" while dozens of tiles were lit on the map.
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
