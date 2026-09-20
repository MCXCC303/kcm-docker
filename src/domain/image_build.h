/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

namespace Kontainer
{

/*!
 * One image build request (ARCH_V5_V8 §5.3).
 *
 * `packBuildContext()` already packed the context into a tar (`contextArchive`); the backend
 * only uploads it and reads progress.
 */
struct ImageBuildRequest {
    /*! Request id: the model keys each build by it, as it keys pulls by reference. */
    QString id;
    /*! Path of the context tar (the caller deletes it after the upload). */
    QString contextArchive;
    /*! Context directory (only for error hints and "build again"). */
    QString contextDirectory;
    /*! Dockerfile path relative to the context (default `Dockerfile`). */
    QString dockerfile = QStringLiteral("Dockerfile");
    /*! Tags (`t` may appear multiple times). */
    QStringList tags;
    /*!
     * Encoded `X-Registry-Auth` (needed when the base image lives in a private registry).
     *
     * The controller reads it from the credential store and encodes it — the same path as pulls;
     * the backend knows nothing about credential stores.
     */
    QByteArray registryAuthHeader;
    QStringList buildArgs; /*!< `KEY=value` entries */
    QList<QPair<QString, QString>> labels;
    QString target;
    QString platform;
    bool noCache = false;
    bool pull = false;
    bool removeIntermediate = false;
};

/*!
 * Increment aggregated from one build-stream line (backend → model).
 *
 * Fields follow "copy the engine verbatim, the UI shows the data": `statusText` / `stepCommand`
 * are never translated.
 */
struct ImageBuildUpdate {
    /*! Whole line as given by the engine (stream or status). */
    QString statusText;
    /*! Current step (1-based) and total steps; 0 when unknown. */
    int stepIndex = 0;
    int totalSteps = 0;
    /*! Raw command of the current step. */
    QString stepCommand;
    /*! Whether the step hit the cache. */
    bool cached = false;
    /*! Known progress (0.0 ~ 1.0); `progressKnown` is false when unknown. */
    double progress = -1.0;
    bool progressKnown = false;
    /*! Failure reason (engine text; empty on success/running), with the failing step's full description. */
    QString errorText;
    /*! Image id reported by the engine on success (`aux.ID`). */
    QString auxImageId;
};

} // namespace Kontainer
