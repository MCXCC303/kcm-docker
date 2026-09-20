/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QString>

namespace Kontainer
{

/*!
 * Shared rules for host port bindings (ARCH_next_ports.md §3).
 *
 * These checks used to live in two places (`OperationController` and `CreateContainerController`
 * each had an "addresses overlap" implementation); port management now serves the create form,
 * the port page and the port topology, so they are consolidated into one set of **pure functions**
 * that nobody may copy again.
 */
namespace PortBindingRules
{

/*!
 * Wildcard addresses: empty string / `0.0.0.0` (all IPv4 interfaces) / `::` or `[::]` (all IPv6).
 *
 * For a mapping without a host address, Docker creates both an IPv4 and an IPv6 binding, and both
 * are wildcards.
 */
bool isWildcardAddress(const QString &hostIp);

/*! Which family a wildcard belongs to: 4 = IPv4, 6 = IPv6, 0 = not a wildcard. */
int wildcardFamily(const QString &hostIp);

/*!
 * Whether two host binding addresses intersect (are mutually exclusive on the same port).
 *
 * A wildcard conflicts with every address (`0.0.0.0:8100` and `[::]:8100` also exclude each other
 * on Linux by default unless `net.ipv6.bindv6only=1` — treat them as conflicting to be safe);
 * two concrete addresses conflict only when **exactly equal** (`127.0.0.1:8100` and
 * `192.168.1.5:8100` can coexist).
 */
bool hostBindingsOverlap(const QString &lhs, const QString &rhs);

/*!
 * Parse the `HostPort` of `HostConfig.PortBindings`: a single port or a range.
 *
 * `WinBoat` was measured using ranges like `"47300-47309"`, so the port model must support them.
 *
 * @param first range start (unchanged when parsing fails)
 * @param last  range end (equals `first` for a single port)
 * @return whether parsing succeeded
 */
bool parseHostPortSpec(const QString &spec, quint16 *first, quint16 *last);

} // namespace PortBindingRules

} // namespace Kontainer
