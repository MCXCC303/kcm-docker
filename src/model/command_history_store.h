/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QObject>
#include <QVariantList>
#include <QStringList>

namespace Kontainer
{

/*!
 * Command history (user test F3: complex commands should be reusable).
 *
 * Two sources:
 *  1. **Local**: every successfully submitted create request records its command (`[CommandHistory]` in
 *     `~/.config/kcm_dockerrc`, max 20 entries, duplicates moved to the front);
 *  2. **Existing containers' commands**: fed in on demand by the UI (`mergeExternal()`), shown as candidates
 *     only, never written to disk -- containers can be deleted at any time, so persisting them is pointless.
 *
 * Stored as one JSON array (commands are multi-line text; a `QStringList` would split them on newlines).
 */
class CommandHistoryStore : public QObject
{
    Q_OBJECT

    /*!
     * Candidate list `[{command, source}]` (`source` is `local` / `container`).
     *
     * Must be a **property** (with NOTIFY): in QML a call like `model: store.entries()` creates no
     * dependency, so Repeater/ComboBox never update (hit once each on logs, networks, volumes, presets).
     */
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
    Q_PROPERTY(bool empty READ empty NOTIFY changed)

public:
    /*! Cap: more than this is unwieldy in a dropdown. */
    static constexpr int kMaxEntries = 20;

    /*! Empty `configPath` uses `QStandardPaths`' `kcm_dockerrc` (tests pass a temp path). */
    explicit CommandHistoryStore(const QString &configPath = {}, QObject *parent = nullptr);

    bool empty() const;
    QVariantList entries() const;
    /*! Plain command list (local first, external after; deduplicated). */
    QStringList commands() const;

    /*! Record a command: empty ignored, duplicate moved to the front, oldest dropped past the cap. */
    Q_INVOKABLE void record(const QString &command);
    /*! Merge external source (existing containers' commands): not persisted, duplicates ignored. */
    Q_INVOKABLE void mergeExternal(const QStringList &commands);
    /*! Clears local records only (external ones are transient and never persisted). */
    Q_INVOKABLE void clearLocal();

Q_SIGNALS:
    void changed();

private:
    void load();
    void save() const;
    /*! Recompute the display candidates (local + external, deduplicated). */
    void rebuild() const;

    QString m_configPath;
    QStringList m_local;
    QStringList m_external;
    /*! Cache (`entries()`/`commands()` are const, so the merged list is computed once here). */
    mutable QStringList m_merged;
};

} // namespace Kontainer
