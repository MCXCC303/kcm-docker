/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
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
 * One host port occupancy (ARCH_next_ports.md §2/§3).
 *
 * Data comes only from the **container list** (`Ports` in `/containers/json`, i.e. the bindings actually
 * published) -- this phase has no `reserved` (what stopped containers declared) and reads no inspect.
 */
struct HostPortEntry {
    /*! Host port (the range start when part of a range). */
    quint16 hostPort = 0;
    /*! Range end; equals `hostPort` for a single port. */
    quint16 hostPortEnd = 0;
    /*! IPv4 wildcard usable (`0.0.0.0` or a concrete IPv4 address). */
    bool ipv4 = false;
    /*! IPv6 wildcard usable (`::`). */
    bool ipv6 = false;
    /*!
     * This row stands for the IPv4 and IPv6 wildcard bindings at once.
     *
     * For a mapping with no host address, Docker creates both `0.0.0.0:<port>` and `[::]:<port>`; the UI
     * (ports page, port topology) shows them as **one** row with a double ring for the two stacks.
     */
    bool dualStack = false;
    /*! Concrete bind address (empty for wildcards; used to display e.g. `127.0.0.1:8100`). */
    QString hostIp;
    /*! Container-side port and protocol. */
    quint16 containerPort = 0;
    QString protocol;
    /*! `inUse` (a running container really published it) / `declaredNotPublished` (not produced here). */
    QString stateKey;
    /*! Occupant. */
    QString containerId;
    QString containerName;
    QString containerImage;
    /*! Container state key (the UI uses it to pick actions such as "navigate / stop"). */
    QString containerStateKey;

    /*! Display address for a row: `20004` (all interfaces) / `127.0.0.1:8100` / `47300-47309`. */
    QString displayAddress() const;
    /*! Port text (a range renders as `47300-47309`). */
    QString portText() const;

    friend bool operator==(const HostPortEntry &lhs, const HostPortEntry &rhs)
    {
        // Note: **display fields must be compared too** (container name/image) -- skipping them means a
        // rename or image change emits no dataChanged and the page keeps the stale value
        // (test refreshKeepsTheModelIntact pins this down)
        return lhs.hostPort == rhs.hostPort && lhs.hostPortEnd == rhs.hostPortEnd && lhs.ipv4 == rhs.ipv4 && lhs.ipv6 == rhs.ipv6
            && lhs.dualStack == rhs.dualStack && lhs.hostIp == rhs.hostIp && lhs.containerPort == rhs.containerPort
            && lhs.protocol == rhs.protocol && lhs.stateKey == rhs.stateKey && lhs.containerId == rhs.containerId
            && lhs.containerName == rhs.containerName && lhs.containerImage == rhs.containerImage
            && lhs.containerStateKey == rhs.containerStateKey;
    }
};

/*!
 * One range segment of the range map (ARCH_next_ports.md §4.B, milestone M4).
 *
 * Port usage clusters into a few bands (e.g. 8000-8010, 20001-20004); drawing all of 0-65535 would show
 * almost nothing but blanks, so adjacent used ports are clustered into segments and drawn per segment.
 */
struct HostPortRange {
    /*! Segment bounds, inclusive; `last - first + 1` is how many ports it covers. */
    quint16 first = 0;
    quint16 last = 0;
    /*! Tiles actually rendered for this segment (capped by `tilesPerRange`). */
    int tileCount = 0;
    /*! Ports not rendered because of the cap (UI shows "N more" when > 0). */
    int hiddenCount = 0;
    /*! How many ports in this segment are held by containers (summary for the title). */
    int usedCount = 0;

    friend bool operator==(const HostPortRange &lhs, const HostPortRange &rhs)
    {
        return lhs.first == rhs.first && lhs.last == rhs.last && lhs.tileCount == rhs.tileCount
            && lhs.hiddenCount == rhs.hiddenCount && lhs.usedCount == rhs.usedCount;
    }
};

/*!
 * Host port occupancy table (ARCH_next_ports.md §3's `HostPortUsage`).
 *
 * A purely functional facade: container list in, "which host port is held by whom" out.
 * The ports page (M3), the range map (M4) and create-form conflict checks (M2) all read from here -- do
 * not reimplement it (`hostBindingsOverlap` once existed twice; see `PortBindingRules`).
 */
class HostPortUsage
{
public:
    /*!
     * Build the occupancy table from the container list.
     *
     * Rules:
     *   - only **running** containers count (`Running` / `Paused` / `Restarting`): a stopped container
     *     holds no host port, and counting its declarations would block other apps (user-confirmed);
     *   - IPv4 and IPv6 wildcard bindings of the same port merge into one row marked `dualStack`;
     *   - sorted by host port, then container name, so rows do not jump on refresh.
     */
    /*!
     * For the ports page: merge "actually published" (container list) with "declared" (inspect, taken
     * only for **running** containers) into one table (user decision 3: declared vs published semantics).
     *
     * - declared and **really published** -> one `inUse` row (published wins; address/port are the real ones)
     * - declared but **not published** (container running) -> one `declaredNotPublished` row
     * - container not running, port free right now -> one `reserved` row
     * - container not running but the port is **already held by another container** -> one `reservedTaken`
     *   row (the user asked for a red "taken" mark -- it would fail on start with a port conflict)
     * - published without a declaration (should not happen) -> still collected as `inUse`
     *
     * `declared` is keyed by container id; when empty the behavior matches the single-argument version.
     */
    static QList<HostPortEntry> entriesFor(const QList<Container> &containers,
                                           const QHash<QString, QList<DeclaredPortBinding>> &declared = {});

    /*!
     * Name of the container holding that host port (empty when none).
     *
     * An empty/wildcard `hostIp` conflicts with any binding (see `PortBindingRules::hostBindingsOverlap`).
     */
    static QString holderFor(const QList<Container> &containers, const QString &hostIp, int hostPort);

    /*!
     * First free host port after `afterPort` (for "suggest a port"; 0 when none is found).
     *
     * Only avoids ports **known to be taken**, and never exceeds 65535; `afterPort <= 0` starts at 8000
     * (below 1024 needs privileges, so it is never suggested).
     *
     * `extraUsed` is for the form being edited: host ports filled in by other rows of the same request
     * must be avoided too, or the suggestion would be rejected on submit as a duplicate within the request.
     */
    static int nextFreePort(const QList<Container> &containers, int afterPort, const QList<int> &extraUsed = {});

    /*!
     * Cluster the occupancy table into ranges (for the range map).
     *
     * Rules:
     *  - with ports ascending, adjacent **used** ports at most `gap` apart join the same segment;
     *  - each segment expands `margin` ports to both sides (so users see where it is still free);
     *  - a segment renders at most `tilesPerRange` tiles and puts the excess into `hiddenCount` (the UI
     *    shows "N more") -- without that cap a range like 1000-1100 drags the UI down.
     */
    static QList<HostPortRange> clusterRanges(const QList<HostPortEntry> &entries, int gap = 5, int margin = 2,
                                             int tilesPerRange = 64);

    /*!
     * State key of a port in the occupancy table (range-map tiles are colored by it).
     *
     * - a range (`47300-47309`) counts as a whole: any port inside it is taken;
     * - one port can carry several rows (e.g. 20003 held by a running container and also declared by a
     *   stopped one): priority is `inUse > reservedTaken > reserved > declaredNotPublished`, so in the
     *   "all ports" view running always beats taken/free (user-requested);
     * - a non-empty `preferred` wins: when the map switches to a filter, only that state's tiles show.
     */
    static QString stateKeyForPort(const QList<HostPortEntry> &entries, quint16 port,
                                   const QStringList &preferred = {});

    /*! Container holding that port (for click-through from the map; empty when none). */
    static HostPortEntry entryForPort(const QList<HostPortEntry> &entries, quint16 port,
                                      const QStringList &preferred = {});
};

} // namespace Kontainer
