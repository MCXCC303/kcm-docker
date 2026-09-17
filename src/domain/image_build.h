/*
    SPDX-FileCopyrightText: 2026 kontainer developers
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
 * 一次镜像构建的请求（ARCH_V5_V8 §5.3）。
 *
 * 上下文已经由 `packBuildContext()` 打成 tar（`contextArchive`），后端只负责上传与读进度。
 */
struct ImageBuildRequest {
    /*! 请求标识：模型里每一条构建用它对应（与拉取按引用对应同理）。 */
    QString id;
    /*! 上下文 tar 的路径（调用方负责在上传结束后删除）。 */
    QString contextArchive;
    /*! 上下文目录（只用于错误提示与"再次构建"）。 */
    QString contextDirectory;
    /*! Dockerfile 在上下文里的相对路径（默认 `Dockerfile`）。 */
    QString dockerfile = QStringLiteral("Dockerfile");
    /*! 标签（`t` 可以出现多次）。 */
    QStringList tags;
    /*!
     * 已编码的 `X-Registry-Auth`（基础镜像在私有仓库时需要）。
     *
     * 由控制器从凭据存储取出并编码——与拉取同一条路径，后端不认识凭据存储。
     */
    QByteArray registryAuthHeader;
    QStringList buildArgs; /*!< `KEY=value` 形式 */
    QList<QPair<QString, QString>> labels;
    QString target;
    QString platform;
    bool noCache = false;
    bool pull = false;
    bool removeIntermediate = false;
};

/*!
 * 构建流的一行聚合出来的增量（后端 → 模型）。
 *
 * 字段都按"引擎原文照搬、界面按数据显示"的约定：`statusText` / `stepCommand` 不翻译。
 */
struct ImageBuildUpdate {
    /*! 引擎给的整行原文（stream 或 status）。 */
    QString statusText;
    /*! 当前步骤号（1 起）与总步数；未知为 0。 */
    int stepIndex = 0;
    int totalSteps = 0;
    /*! 当前步骤的命令原文。 */
    QString stepCommand;
    /*! 该步骤是否命中缓存。 */
    bool cached = false;
    /*! 已知进度（0.0 ~ 1.0）；未知时 `progressKnown` 为 false。 */
    double progress = -1.0;
    bool progressKnown = false;
    /*! 失败原因（引擎原文；成功与进行中为空）。含失败步骤的完整描述会被写在这里。 */
    QString errorText;
    /*! 构建成功时引擎给出的镜像 id（`aux.ID`）。 */
    QString auxImageId;
};

} // namespace Kontainer
