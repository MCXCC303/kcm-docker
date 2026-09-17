/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/volume_detail_controller.h"

namespace Kontainer
{

VolumeDetailController::VolumeDetailController(DockerBackendInterface *backend, QObject *parent)
    : QObject(parent)
    , m_backend(backend)
    , m_labels(new DetailListModel(this))
    , m_options(new DetailListModel(this))
{
    Q_ASSERT(m_backend);
    // 列表刷新后重新取快照：详情页显示的是"当前这一刻"的卷
    connect(m_backend, &DockerBackendInterface::volumesUpdated, this, &VolumeDetailController::reload);
}

QString VolumeDetailController::volumeName() const
{
    return m_name;
}

bool VolumeDetailController::valid() const
{
    return m_valid;
}

QString VolumeDetailController::name() const
{
    return m_volume.name;
}

QString VolumeDetailController::driver() const
{
    return m_volume.driver;
}

QString VolumeDetailController::mountpoint() const
{
    return m_volume.mountpoint;
}

QDateTime VolumeDetailController::created() const
{
    return m_volume.createdAt;
}

QString VolumeDetailController::scope() const
{
    return m_volume.scope;
}

qint64 VolumeDetailController::sizeBytes() const
{
    return m_volume.sizeBytes;
}

bool VolumeDetailController::sizeKnown() const
{
    return m_volume.sizeKnown();
}

int VolumeDetailController::refCount() const
{
    return m_volume.refCount;
}

bool VolumeDetailController::usageKnown() const
{
    return m_volume.usageKnown();
}

bool VolumeDetailController::inUse() const
{
    return m_volume.isInUse();
}

QString VolumeDetailController::status() const
{
    return m_volume.status;
}

bool VolumeDetailController::mountpointUsable() const
{
    return m_valid && !m_volume.mountpoint.isEmpty();
}

DetailListModel *VolumeDetailController::labels() const
{
    return m_labels;
}

DetailListModel *VolumeDetailController::options() const
{
    return m_options;
}

void VolumeDetailController::selectVolume(const QString &name)
{
    if (m_name == name) {
        reload();
        return;
    }
    m_name = name;
    reload();
    Q_EMIT changed();
}

void VolumeDetailController::reload()
{
    Volume found;
    bool valid = false;
    if (!m_name.isEmpty()) {
        const QList<Volume> volumes = m_backend->volumes();
        for (const Volume &volume : volumes) {
            if (volume.name == m_name) {
                found = volume;
                valid = true;
                break;
            }
        }
    }

    const bool changedState = m_valid != valid || !(m_volume == found);
    m_volume = found;
    m_valid = valid;

    QList<DetailEntry> labelEntries;
    labelEntries.reserve(found.labels.size());
    for (const auto &label : found.labels) {
        labelEntries.append({label.first, label.second, QString(), QString()});
    }
    m_labels->setEntries(labelEntries);

    QList<DetailEntry> optionEntries;
    optionEntries.reserve(found.options.size());
    for (const auto &option : found.options) {
        optionEntries.append({option.first, option.second, QString(), QString()});
    }
    m_options->setEntries(optionEntries);

    if (changedState) {
        Q_EMIT changed();
    }
}

} // namespace Kontainer
