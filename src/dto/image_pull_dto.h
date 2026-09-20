/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "domain/image_pull_progress.h"

#include <QJsonObject>
#include <QString>

namespace Kontainer
{

/*!
 * One line of the image pull stream (`POST /images/create` answers with JSON lines).
 *
 * Only field extraction and engine status text → Phase mapping; aggregation lives in the backend.
 */
struct DockerImagePullLineDTO {
    QString id;
    QString status;
    qint64 current = 0;
    qint64 total = 0;
    bool hasProgress = false;
    QString error;
    QString errorDetail;

    static DockerImagePullLineDTO fromJson(const QJsonObject &object);
    /*! Engine status text → phase; Waiting when unrecognized. */
    static ImagePullProgress::Phase phaseForStatus(const QString &status);
    /*! Whether this line marks the layer as finished (downloaded / already exists / pulled). */
    bool layerFinished() const;
    /*!
     * Whether this line reports layer-level progress.
     *
     * Lines like "Pulling from library/alpine" also carry an id (the tag, not a layer); counting
     * them as a layer inflates the progress denominator. Only download / extract / verify /
     * waiting style statuses belong to a layer.
     */
    bool isLayerStatus() const;
};

} // namespace Kontainer
