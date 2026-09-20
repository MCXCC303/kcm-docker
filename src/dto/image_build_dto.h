/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QJsonObject>
#include <QString>

namespace Kontainer
{

/*!
 * One line of the build stream (`POST /build` answers with JSON lines, same shape as pull).
 *
 * Field extraction only: the real aggregation happens in the backend and the models.
 */
struct DockerImageBuildLineDTO {
    /*! `stream`: one line of build output (carries `Step 1/5 : FROM …` and the like). */
    QString stream;
    /*! `status`: some engine versions report progress here. */
    QString status;
    /*! In-stream error (HTTP stays 200; the failure text is in here). */
    QString error;
    QString errorDetail;
    /*! `aux.ID`: image id returned on a successful build. */
    QString auxImageId;
    qint64 current = 0;
    qint64 total = 0;
    bool hasProgress = false;

    /* ---- Build steps parsed out of `stream` (needed to locate failures, §5.3) ---- */
    /*! Step number, 1-based; 0 when it cannot be parsed. */
    int stepIndex = 0;
    /*! Total number of steps; 0 when it cannot be parsed. */
    int totalSteps = 0;
    /*! Raw command of this step (`FROM alpine:3.19`). */
    QString stepCommand;
    /*! Cache hint for this step (`Using cache`, `CACHED`) — display only. */
    bool cached = false;

    static DockerImageBuildLineDTO fromJson(const QJsonObject &object);
    /*! Parse lines like `Step 3/7 : RUN make`; a non-step line leaves `stepCommand` empty. */
    void parseStepLine();
};

} // namespace Kontainer
