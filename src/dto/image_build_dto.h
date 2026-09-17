/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QJsonObject>
#include <QString>

namespace Kontainer
{

/*!
 * 构建流的一行（`POST /build` 的响应是逐行 JSON，形态与拉取一致）。
 *
 * 只做字段提取：真正的聚合在 backend 与模型里。
 */
struct DockerImageBuildLineDTO {
    /*! `stream`：构建输出的一行（含 `Step 1/5 : FROM …` 这类信息）。 */
    QString stream;
    /*! `status`：一些引擎版本用 status 表达进度。 */
    QString status;
    /*! 流内错误（HTTP 仍是 200，失败信息在这里）。 */
    QString error;
    QString errorDetail;
    /*! `aux.ID`：构建成功时给出的镜像 id。 */
    QString auxImageId;
    qint64 current = 0;
    qint64 total = 0;
    bool hasProgress = false;

    /* ---- 从 `stream` 里解析出来的构建步骤（失败定位要用，§5.3） ---- */
    /*! 第几步（从 1 开始）；解析不出来为 0。 */
    int stepIndex = 0;
    /*! 共几步；解析不出来为 0。 */
    int totalSteps = 0;
    /*! 该步的命令原文（`FROM alpine:3.19`）。 */
    QString stepCommand;
    /*! 该步的缓存提示（`Using cache`、`CACHED`）——只作展示。 */
    bool cached = false;

    static DockerImageBuildLineDTO fromJson(const QJsonObject &object);
    /*! 解析 `Step 3/7 : RUN make` 这类行；不是步骤行时把参数原样留在 `stepCommand` 为空。 */
    void parseStepLine();
};

} // namespace Kontainer
