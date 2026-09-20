/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QMetaType>
#include <QString>

namespace Kontainer
{

/*!
 * Image pull progress (ARCH_V4 §2.2.4).
 *
 * A domain object: numbers and engine text only, no UI strings.
 * `statusText` / `errorText` are engine strings ("Downloading", "manifest unknown", …), treated
 * like container status ("Up 2 hours"): shown as data, never translated.
 */
struct ImagePullProgress {
    enum class Phase {
        Waiting,
        Downloading,
        Extracting,
        Verifying,
        Complete,
        Failed,
    };

    /*! User-requested reference (normalized), used in result texts. */
    QString reference;
    Phase phase = Phase::Waiting;
    /*! Layer being processed (engine's short id, may be empty). */
    QString layerId;
    /*! Status text as given by the engine. */
    QString statusText;
    /*! Bytes downloaded (sum over all layers). */
    qint64 currentBytes = 0;
    /*! Known total (sum of layers that reported a total); 0 = unknown. */
    qint64 totalBytes = 0;
    int completedLayers = 0;
    int totalLayers = 0;
    /*! Error line from the stream (engine text); non-empty means the pull failed. */
    QString errorText;

    bool isValid() const
    {
        return !reference.isEmpty();
    }
    /*! With an unknown total the progress bar must be indeterminate (ARCH_V4 §2.4). */
    bool isIndeterminate() const
    {
        return totalBytes <= 0;
    }
    /*! 0.0 ~ 1.0; -1 when unknown. */
    double fraction() const
    {
        if (totalBytes <= 0) {
            return -1.0;
        }
        return qBound(0.0, double(currentBytes) / double(totalBytes), 1.0);
    }
    bool failed() const
    {
        return !errorText.isEmpty() || phase == Phase::Failed;
    }
};

} // namespace Kontainer

Q_DECLARE_METATYPE(Kontainer::ImagePullProgress)
