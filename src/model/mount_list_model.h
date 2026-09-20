/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>

namespace Kontainer
{

/*!
 * One mount entry (ARCH_V4 §2.1.1).
 *
 * Unlike `DetailEntry` (label/value/detail text) a mount row needs **structured fields**: type
 * badge, rw mode, host path, container path, named volume, whether the host path really exists,
 * and whether it can be opened. Squeezing those into three strings would make QML parse text back.
 */
struct MountEntry {
    /*! bind / volume / tmpfs (raw engine text, lowercase). */
    QString typeKey;
    /*! Host path; empty for tmpfs. */
    QString source;
    /*! Path inside the container. */
    QString destination;
    /*! rw / ro. */
    QString mode;
    /*! Named volume name; empty for bind mounts and anonymous volumes. */
    QString volumeName;
    /*! directory / missing / notADirectory / notApplicable. */
    QString sourceStateKey;

    bool isOpenable() const
    {
        return sourceStateKey == QLatin1String("directory");
    }

    friend bool operator==(const MountEntry &lhs, const MountEntry &rhs)
    {
        return lhs.typeKey == rhs.typeKey && lhs.source == rhs.source && lhs.destination == rhs.destination && lhs.mode == rhs.mode
            && lhs.volumeName == rhs.volumeName && lhs.sourceStateKey == rhs.sourceStateKey;
    }
};

/*!
 * Mount list model (ARCH_V4 §2.1.1).
 *
 * Presentation data only; the controller fills in host-path probe results (the model does no I/O).
 * Follows the `DetailListModel` convention of emitting nothing when unchanged — the detail page
 * re-checks every 30 seconds, and an unconditional reset would destroy and rebuild QML rows over
 * and over (the segfault trigger in ARCH_V3 appendix A.1g).
 */
class MountListModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool empty READ empty NOTIFY countChanged)
    /*! Mounts that cannot be opened (no host path, or it is missing) so the page can warn. */
    Q_PROPERTY(int blockedCount READ blockedCount NOTIFY countChanged)

public:
    enum Roles {
        TypeKeyRole = Qt::UserRole + 1,
        SourceRole,
        DestinationRole,
        ModeRole,
        VolumeNameRole,
        SourceStateKeyRole,
        OpenableRole,
    };
    Q_ENUM(Roles)

    explicit MountListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    bool empty() const;
    int blockedCount() const;

    void setMounts(const QList<MountEntry> &mounts);
    const QList<MountEntry> &mounts() const
    {
        return m_mounts;
    }

Q_SIGNALS:
    void countChanged();

private:
    QList<MountEntry> m_mounts;
};

} // namespace Kontainer
