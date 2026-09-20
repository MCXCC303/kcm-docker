/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "domain/volume.h"

#include "model/keyed_list_model.h"
#include <QList>

namespace Kontainer
{

/*!
 * Volume list model (ARCH_V5_V8 §3.5).
 *
 * Same conventions as the other resource lists: no signal when nothing changed,
 * sorting/filtering left to the proxy; every list field (name, driver, mountpoint, size,
 * refs, in-use) and detail field (labels, options, status) is a role -- `/volumes` has it all.
 */
class VolumeModel : public KeyedListModel<VolumeModel, Volume>
{
    Q_OBJECT

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool empty READ empty NOTIFY countChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        DriverRole,
        MountpointRole,
        CreatedRole,
        ScopeRole,
        /*! Bytes used; -1 = unknown (UI shows "--", not 0). */
        SizeBytesRole,
        SizeKnownRole,
        /*! Containers using it; -1 = unknown. */
        RefCountRole,
        /*! Whether a container **definitely** uses it (false when unknown). */
        InUseRole,
        UsageKnownRole,
        LabelsRole,
        OptionsRole,
        StatusRole,
    };
    Q_ENUM(Roles)

    explicit VolumeModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    bool empty() const;
    const QList<Volume> &volumes() const;
    /*! Leaves the model untouched when the content is unchanged. */
    void setVolumes(const QList<Volume> &volumes);
    void clear();

    /*! Row for a name; -1 when absent. */
    Q_INVOKABLE int rowForName(const QString &name) const;
    /*! All names (the UI needs a list even without delegates, e.g. to confirm a delete). */
    Q_INVOKABLE QStringList names() const;
    /*! Unused volumes (for the prune preview, **not** derived from the engine's prune result). */
    Q_INVOKABLE QStringList unusedNames() const;
    /*! Plain-data summary `[{name, driver, mountpoint, inUse}]` (panels/dialogs). */
    Q_INVOKABLE QVariantList summaries() const;
    /*! Sum of **known** sizes among unused volumes (cleanup preview; unknown excluded). */
    Q_INVOKABLE qint64 knownUnusedSize() const;
    /*! Count of unused volumes with unknown size (the preview must state "N more unknown"). */
    Q_INVOKABLE int unknownUnusedSizeCount() const;

Q_SIGNALS:
    void countChanged();

private:
    QList<Volume> m_volumes;
};

} // namespace Kontainer
