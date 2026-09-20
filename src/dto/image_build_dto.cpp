/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
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
    // Classic builder step lines look like `Step 3/7 : RUN make` (a cache hint may follow)
    static const QRegularExpression stepPattern(
        QStringLiteral(R"(^\s*Step\s+(\d+)\s*/\s*(\d+)\s*:\s*(.*)$)"));
    const QRegularExpressionMatch match = stepPattern.match(stream);
    if (!match.hasMatch()) {
        // A standalone cache hint (` ---> Using cache`) belongs to the latest step; just flag it
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
