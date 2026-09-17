/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "dto/image_pull_dto.h"

#include "dto/json_helpers.h"

namespace Kontainer
{

using namespace JsonHelpers;

DockerImagePullLineDTO DockerImagePullLineDTO::fromJson(const QJsonObject &object)
{
    DockerImagePullLineDTO line;
    line.id = stringValue(object, QStringLiteral("id"));
    line.status = stringValue(object, QStringLiteral("status"));

    const QJsonValue progress = object.value(QStringLiteral("progressDetail"));
    if (progress.isObject()) {
        const QJsonObject detail = progress.toObject();
        line.current = integerValue(detail, QStringLiteral("current"));
        line.total = integerValue(detail, QStringLiteral("total"));
        line.hasProgress = line.total > 0;
    }

    line.error = stringValue(object, QStringLiteral("error"));
    const QJsonValue errorDetail = object.value(QStringLiteral("errorDetail"));
    if (errorDetail.isObject()) {
        line.errorDetail = stringValue(errorDetail.toObject(), QStringLiteral("message"));
    }
    if (line.error.isEmpty()) {
        line.error = line.errorDetail;
    }

    return line;
}

ImagePullProgress::Phase DockerImagePullLineDTO::phaseForStatus(const QString &status)
{
    // 引擎的措辞（"Downloading"、"Extracting"、"Verifying Checksum"…）是稳定的，
    // 但这里只用来选阶段；真正显示给用户的是引擎原文（按数据处理）。
    if (status.contains(QLatin1String("Downloading"), Qt::CaseInsensitive)) {
        return ImagePullProgress::Phase::Downloading;
    }
    if (status.contains(QLatin1String("Extracting"), Qt::CaseInsensitive)) {
        return ImagePullProgress::Phase::Extracting;
    }
    if (status.contains(QLatin1String("Verifying"), Qt::CaseInsensitive)) {
        return ImagePullProgress::Phase::Verifying;
    }
    if (status.contains(QLatin1String("Pull complete"), Qt::CaseInsensitive)
        || status.contains(QLatin1String("Already exists"), Qt::CaseInsensitive)) {
        return ImagePullProgress::Phase::Complete;
    }
    return ImagePullProgress::Phase::Waiting;
}

bool DockerImagePullLineDTO::layerFinished() const
{
    return status.contains(QLatin1String("Download complete"), Qt::CaseInsensitive)
        || status.contains(QLatin1String("Pull complete"), Qt::CaseInsensitive)
        || status.contains(QLatin1String("Already exists"), Qt::CaseInsensitive);
}

bool DockerImagePullLineDTO::isLayerStatus() const
{
    if (hasProgress || layerFinished()) {
        return true;
    }
    return status.contains(QLatin1String("Downloading"), Qt::CaseInsensitive)
        || status.contains(QLatin1String("Extracting"), Qt::CaseInsensitive)
        || status.contains(QLatin1String("Verifying"), Qt::CaseInsensitive)
        || status.contains(QLatin1String("Waiting"), Qt::CaseInsensitive)
        || status.contains(QLatin1String("Pulling fs layer"), Qt::CaseInsensitive);
}

} // namespace Kontainer
