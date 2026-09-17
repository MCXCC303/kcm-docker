/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "domain/image_pull_progress.h"

#include <QJsonObject>
#include <QString>

namespace Kontainer
{

/*!
 * 镜像拉取流的一行（`POST /images/create` 的响应是逐行 JSON）。
 *
 * 只做字段提取与引擎状态文本 → Phase 的映射，不做聚合（聚合在 backend）。
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
    /*! 引擎状态文本 → 阶段；识别不了时返回 Waiting。 */
    static ImagePullProgress::Phase phaseForStatus(const QString &status);
    /*! 该行是否表示这一层已经完成（下载完 / 已存在 / 拉取完）。 */
    bool layerFinished() const;
    /*!
     * 该行是否是「层」级进度。
     *
     * "Pulling from library/alpine" 这类行也带 id（id 是 tag，不是层），
     * 如果把它算成一层，进度分母会凭空多一层。只有下载 / 解压 / 校验 / 等待
     * 这类状态才属于层。
     */
    bool isLayerStatus() const;
};

} // namespace Kontainer
