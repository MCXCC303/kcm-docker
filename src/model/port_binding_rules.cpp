/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/port_binding_rules.h"

namespace Kontainer
{

namespace PortBindingRules
{

bool isWildcardAddress(const QString &hostIp)
{
    return hostIp.isEmpty() || hostIp == QLatin1String("0.0.0.0") || hostIp == QLatin1String("::")
        || hostIp == QLatin1String("[::]");
}

int wildcardFamily(const QString &hostIp)
{
    if (hostIp.isEmpty() || hostIp == QLatin1String("0.0.0.0")) {
        return 4;
    }
    if (hostIp == QLatin1String("::") || hostIp == QLatin1String("[::]")) {
        return 6;
    }
    return 0;
}

bool hostBindingsOverlap(const QString &lhs, const QString &rhs)
{
    const QString left = lhs.isEmpty() ? QStringLiteral("0.0.0.0") : lhs;
    const QString right = rhs.isEmpty() ? QStringLiteral("0.0.0.0") : rhs;
    if (isWildcardAddress(left) || isWildcardAddress(right)) {
        return true;
    }
    return left == right;
}

bool parseHostPortSpec(const QString &spec, quint16 *first, quint16 *last)
{
    const QString trimmed = spec.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    const int dash = trimmed.indexOf(QLatin1Char('-'));
    const QString firstText = dash < 0 ? trimmed : trimmed.left(dash);
    const QString lastText = dash < 0 ? trimmed : trimmed.mid(dash + 1);

    bool okFirst = false;
    bool okLast = false;
    const int start = firstText.toInt(&okFirst);
    const int end = dash < 0 ? start : lastText.toInt(&okLast);
    if (!okFirst || start <= 0 || start > 65535) {
        return false;
    }
    if (dash >= 0 && (!okLast || end < start || end > 65535)) {
        return false;
    }
    if (first) {
        *first = quint16(start);
    }
    if (last) {
        *last = quint16(end);
    }
    return true;
}

} // namespace PortBindingRules

} // namespace Kontainer
