/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "domain/container_create_request.h"

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

namespace Kontainer
{

/*!
 * One mount preset (ARCH_V5_V8 §4.1).
 *
 * A preset has a **stable id** (not a list index): add/remove/update/reorder all locate by it,
 * otherwise one reorder would scramble which entry is being edited.
 */
struct MountPreset {
    QString id;
    /*! Host path for bind / volume name for volume; empty for tmpfs. */
    QString source;
    QString destination;
    /*! bind / volume / tmpfs */
    QString type = QStringLiteral("bind");
    bool readOnly = false;
    /*! Note written by the user, e.g. "frontend config directory". */
    QString note;
    /*! Favorite: shown first. */
    bool favorite = false;
    /*! Last time used (empty when never used). */
    QDateTime lastUsedAt;

    /*! Whether it points at the same place as a mount request (for dedup and "already added"). */
    bool matches(const ContainerMountRequest &request) const;

    friend bool operator==(const MountPreset &lhs, const MountPreset &rhs)
    {
        return lhs.id == rhs.id && lhs.source == rhs.source && lhs.destination == rhs.destination
            && lhs.type == rhs.type && lhs.readOnly == rhs.readOnly && lhs.note == rhs.note
            && lhs.favorite == rhs.favorite && lhs.lastUsedAt == rhs.lastUsedAt;
    }
};

/*!
 * Persistence for mount presets (`~/.config/kcm_dockerrc`, group `[MountPresets]`).
 *
 * Not the KCM config module: presets are **this tool's data**, not system settings (§1.5.3).
 *
 * The storage shape is deliberately plain: one `KConfig` group per preset
 * (`[MountPresets][<id>]`) plus an `Order` key recording the sequence — easier to inspect with
 * tools like kreadconfig than JSON stuffed into one key, and one bad character cannot lose every
 * preset.
 */
class MountPresetStore : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(bool empty READ empty NOTIFY changed)
    /*!
     * Preset summaries as a **property**, not just Q_INVOKABLE.
     *
     * `model: store.summaries()` in QML is a function call that **creates no dependency**: the
     * Repeater reads it once at creation, so later additions/removals never rebuild it (observed:
     * existing presets invisible in the preset tab). Only a property with NOTIFY keeps up.
     * This trap has already been hit four times (logs, networks, volumes, preview button).
     */
    Q_PROPERTY(QVariantList summaries READ summariesProperty NOTIFY changed)

public:
    /*! Recent-use cap (§4.1): beyond it, evict the oldest **non-favorite** entries. */
    static constexpr int kMaxRecent = 20;

    /*!
     * An empty `configPath` uses `QStandardPaths`' `kcm_dockerrc`.
     *
     * Tests pass a temporary path, so the user's config is not touched.
     */
    explicit MountPresetStore(const QString &configPath = {}, QObject *parent = nullptr);

    int count() const;
    bool empty() const;
    /*! Favorites first, then most recently used (never-used entries in insertion order). */
    QList<MountPreset> presets() const;
    /*! Plain-data summary for the UI: `[{id, source, destination, type, readOnly, note, favorite}]`. */
    Q_INVOKABLE QVariantList summaries() const;
    /*! Same as above, in property form (QML Repeaters require a property). */
    QVariantList summariesProperty() const
    {
        return summaries();
    }

    /*! Add; returns the existing id when host + container path already match (no duplicate added). */
    Q_INVOKABLE QString add(const QString &source,
                            const QString &destination,
                            const QString &type = QStringLiteral("bind"),
                            bool readOnly = false,
                            const QString &note = {});
    Q_INVOKABLE bool remove(const QString &id);
    Q_INVOKABLE bool update(const QString &id,
                            const QString &source,
                            const QString &destination,
                            bool readOnly,
                            const QString &note);
    Q_INVOKABLE bool setFavorite(const QString &id, bool favorite);
    /*! Move up / down, reordering within the favorite or recent group. */
    Q_INVOKABLE bool moveUp(const QString &id);
    Q_INVOKABLE bool moveDown(const QString &id);
    /*! Called after a successful create: merge these mounts into the presets and refresh "recent". */
    Q_INVOKABLE void noteUsed(const QList<Kontainer::ContainerMountRequest> &mounts);

    /*! Validation (stable keys): host path absolute or volume name valid, container path absolute, dedup. */
    static QString validateSource(const QString &source, const QString &type);
    static QString validateDestination(const QString &destination);
    /*!
     * **Instance versions** of the two above: QML only reaches Q_INVOKABLE / slots / properties,
     * so static member functions are undefined there (calling one throws a TypeError).
     */
    Q_INVOKABLE QString sourceError(const QString &source, const QString &type) const
    {
        return validateSource(source, type);
    }
    Q_INVOKABLE QString destinationError(const QString &destination) const
    {
        return validateDestination(destination);
    }

Q_SIGNALS:
    void changed();

private:
    void load();
    void save() const;
    /*! Generate an id that does not collide with existing ones. */
    QString nextId() const;
    void trimRecents();

    QString m_configPath;
    QList<MountPreset> m_presets;
    /*! Explicit order (id list): matches the m_presets display order, written to `Order` on save. */
    QStringList m_order;
};

} // namespace Kontainer
