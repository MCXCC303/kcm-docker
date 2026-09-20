/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/docker_backend_interface.h"
#include "domain/volume.h"
#include "model/detail_list_model.h"

#include <QObject>
#include <QString>

namespace Kontainer
{

/*!
 * Controller for the volume detail page (ARCH_V5_V8 §3.5).
 *
 * Data comes from the volume list the backend already holds (full objects), so the
 * detail page sends **no extra request**: it re-snapshots on selection or list change.
 *
 * Read-only info is split into two `DetailListModel`s: labels and driver options.
 */
class VolumeDetailController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString volumeName READ volumeName NOTIFY changed)
    /*! Whether the selected volume still exists (false after delete/refresh; UI hides the detail). */
    Q_PROPERTY(bool valid READ valid NOTIFY changed)

    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(QString driver READ driver NOTIFY changed)
    Q_PROPERTY(QString mountpoint READ mountpoint NOTIFY changed)
    Q_PROPERTY(QDateTime created READ created NOTIFY changed)
    Q_PROPERTY(QString scope READ scope NOTIFY changed)
    /*! Bytes used; -1 = unknown (UI shows "--"). */
    Q_PROPERTY(qint64 sizeBytes READ sizeBytes NOTIFY changed)
    Q_PROPERTY(bool sizeKnown READ sizeKnown NOTIFY changed)
    Q_PROPERTY(int refCount READ refCount NOTIFY changed)
    Q_PROPERTY(bool usageKnown READ usageKnown NOTIFY changed)
    Q_PROPERTY(bool inUse READ inUse NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    /*! Whether the mountpoint can be opened in the file manager (an empty one cannot). */
    Q_PROPERTY(bool mountpointUsable READ mountpointUsable NOTIFY changed)

    Q_PROPERTY(Kontainer::DetailListModel *labels READ labels CONSTANT)
    Q_PROPERTY(Kontainer::DetailListModel *options READ options CONSTANT)

public:
    explicit VolumeDetailController(DockerBackendInterface *backend, QObject *parent = nullptr);

    QString volumeName() const;
    bool valid() const;
    QString name() const;
    QString driver() const;
    QString mountpoint() const;
    QDateTime created() const;
    QString scope() const;
    qint64 sizeBytes() const;
    bool sizeKnown() const;
    int refCount() const;
    bool usageKnown() const;
    bool inUse() const;
    QString status() const;
    bool mountpointUsable() const;

    DetailListModel *labels() const;
    DetailListModel *options() const;

    /*! Select a volume (an empty name clears the selection). */
    Q_INVOKABLE void selectVolume(const QString &name);

Q_SIGNALS:
    void changed();

private:
    void reload();

    DockerBackendInterface *m_backend = nullptr;
    QString m_name;
    Volume m_volume;
    bool m_valid = false;

    DetailListModel *m_labels = nullptr;
    DetailListModel *m_options = nullptr;
};

} // namespace Kontainer
