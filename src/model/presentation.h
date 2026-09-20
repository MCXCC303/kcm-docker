/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QObject>
#include <QString>

namespace Kontainer
{

/*!
 * Presentation helpers shared with QML (ARCH_V2 §12/§38/§41).
 *
 * Answers "semantic" questions only (positive / neutral / negative, icon names); the actual colour
 * still comes from Kirigami.Theme in QML (§12: colours must come from the KDE palette, never
 * hard-coded RGB). State colour/icon rules are written once and shared by cards, detail pages and
 * the overview.
 */
class Presentation : public QObject
{
    Q_OBJECT

public:
    explicit Presentation(QObject *parent = nullptr);

    /*!
     * Stable palette index for a seed string (used for port topology lines).
     *
     * Purpose: the node graph of "the same container" always uses the same line colour (the look
     * agreed in ARCH_V4 §2.1.2) — the seed is usually the container id, so refreshing, reopening the
     * page or changing the theme never alters it. The colour carries no meaning (the chip text at
     * each end is the information), so it only has to be stable and evenly distributed.
     *
     * Algorithm: FNV-1a (32-bit) then modulo — a pure function, identical across platforms and
     * independent of the Qt version. Returns 0 when `paletteSize <= 0` or the seed is empty.
     */
    Q_INVOKABLE int connectionColorIndex(const QString &seed, int paletteSize) const;

    /*! State + health → semantic colour key: positive / neutral / negative / disabled. */
    Q_INVOKABLE QString stateSemanticKey(const QString &stateKey, const QString &healthKey) const;

    /*! Container state → icon name (icon theme name). */
    Q_INVOKABLE QString stateIconName(const QString &stateKey) const;

    /*!
     * State key → user-visible text (`running` → "Running").
     *
     * The detail pages' "used by / network members" lists also show state, and the text is defined
     * in exactly one C++ place (`state_text.cpp`, the same one the container list uses).
     */
    Q_INVOKABLE QString stateText(const QString &stateKey) const;

    /*! Health state → icon name; empty when there is no health check. */
    Q_INVOKABLE QString healthIconName(const QString &healthKey) const;

    /*!
     * Copy to the clipboard (ARCH_V2 §41).
     * Only for identifiers such as Container ID / Image ID / IP / Port; there is deliberately no
     * "copy the whole inspect JSON" action.
     */
    Q_INVOKABLE void copyToClipboard(const QString &text) const;

    /* --- Key/value and list editor validation (ARCH_V5_V8 §1.6: a single implementation) --- */

    /*!
     * Whether a key is valid: `[A-Za-z_][A-Za-z0-9_]*`.
     * Environment variables, labels and build args share this one rule.
     */
    Q_INVOKABLE bool isValidEnvKey(const QString &key) const;

    /*!
     * Parse `.env`-style text into `[{ key, value }]`.
     *
     * Handles comments (`#`), blank lines, an `export ` prefix, single/double-quoted values and
     * trailing `#` comments. Unparseable lines are skipped rather than reported: pasted content is
     * often semi-structured human text.
     */
    Q_INVOKABLE QVariantList parseEnvText(const QString &text) const;

    /*! Whether the port is in range (1–65535). */
    Q_INVOKABLE bool isValidPort(int port) const;

    /*!
     * Whether a host port conflicts with an existing binding.
     *
     * `usedBindings` is a list of binding strings such as `0.0.0.0:8080`, `127.0.0.1:8080`.
     * Rule: the same port on the same IP conflicts, and a wildcard address (`0.0.0.0` / `::` /
     * empty) conflicts with any concrete IP on that port. This is the only conflict check; the
     * create form and the port editor both call it.
     */
    Q_INVOKABLE bool hostPortConflicts(const QString &hostIp, int hostPort, const QStringList &usedBindings) const;

    /*! Whether the host IP is a wildcard address (`0.0.0.0` / `::` / empty string). */
    Q_INVOKABLE bool isWildcardHostIp(const QString &hostIp) const;

    /* --- Runtime config (daemon.json) field validation (ARCH_V5_V8 §2.3) --- */

    /*!
     * Validation result key for a registry mirror address.
     *
     * A key, not text: the rules live in C++ (testable, single copy) and the wording in QML
     * (translatable). Empty string = valid; `emptyHost` = empty input; `invalid` = malformed.
     */
    Q_INVOKABLE QString registryMirrorErrorKey(const QString &value) const;

    /*! Validation result key for insecure-registries (accepts `host[:port]` only, no scheme or path). */
    Q_INVOKABLE QString insecureRegistryErrorKey(const QString &value) const;
};

} // namespace Kontainer
