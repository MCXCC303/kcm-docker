/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "dto/image_build_dto.h"

#include "dto/json_helpers.h"

#include <QJsonValue>
#include <QRegularExpression>

namespace Kontainer
{

DockerImageBuildLineDTO DockerImageBuildLineDTO::fromJson(const QJsonObject &object)
{
    DockerImageBuildLineDTO line;
    line.stream = object.value(QStringLiteral("stream")).toString();
    line.status = object.value(QStringLiteral("status")).toString();
    line.error = object.value(QStringLiteral("error")).toString();
    line.errorDetail = object.value(QStringLiteral("errorDetail")).toObject().value(QStringLiteral("message")).toString();
    line.auxImageId = object.value(QStringLiteral("aux")).toObject().value(QStringLiteral("ID")).toString();

    const QJsonObject progress = object.value(QStringLiteral("progressDetail")).toObject();
    if (progress.contains(QStringLiteral("current")) || progress.contains(QStringLiteral("total"))) {
        line.current = qint64(progress.value(QStringLiteral("current")).toDouble());
        line.total = qint64(progress.value(QStringLiteral("total")).toDouble());
        line.hasProgress = line.total > 0;
    }

    line.parseStepLine();
    return line;
}

void DockerImageBuildLineDTO::parseStepLine()
{
    if (stream.isEmpty()) {
        return;
    }
    // 经典构建器的步骤行形如：`Step 3/7 : RUN make`（也有 `Step 3/7 : RUN make` 后跟缓存提示）
    static const QRegularExpression stepPattern(
        QStringLiteral(R"(^\s*Step\s+(\d+)\s*/\s*(\d+)\s*:\s*(.*)$)"));
    const QRegularExpressionMatch match = stepPattern.match(stream);
    if (!match.hasMatch()) {
        // 缓存提示单独出现（` ---> Using cache`）时附在最近一步上，这里只标记出来
        if (stream.contains(QLatin1String("Using cache")) || stream.contains(QLatin1String("CACHED"))) {
            cached = true;
        }
        return;
    }
    stepIndex = match.captured(1).toInt();
    totalSteps = match.captured(2).toInt();
    stepCommand = match.captured(3).trimmed();
}

} // namespace Kontainer
