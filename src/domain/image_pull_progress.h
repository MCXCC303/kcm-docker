/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QMetaType>
#include <QString>

namespace Kontainer
{

/*!
 * 镜像拉取进度（ARCH_V4 §2.2.4）。
 *
 * 这是 domain 对象：只保存数值与引擎原文，不含任何 UI 文案。
 * `statusText` / `errorText` 是引擎返回的字符串（"Downloading"、"manifest unknown" 等），
 * 与容器 status（"Up 2 hours"）同等对待——按数据显示，不翻译。
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

    /*! 用户请求的引用（归一化后），用于结果文案。 */
    QString reference;
    Phase phase = Phase::Waiting;
    /*! 当前正在处理的层（引擎给的前缀 id，可能为空）。 */
    QString layerId;
    /*! 引擎给的状态原文。 */
    QString statusText;
    /*! 已下载字节（所有层之和）。 */
    qint64 currentBytes = 0;
    /*! 已知总量（所有已报告 total 的层之和）；0 表示未知。 */
    qint64 totalBytes = 0;
    int completedLayers = 0;
    int totalLayers = 0;
    /*! 流内 error 行（引擎原文）；非空表示这次拉取失败。 */
    QString errorText;

    bool isValid() const
    {
        return !reference.isEmpty();
    }
    /*! 总量未知时进度条应为不确定态（ARCH_V4 §2.4）。 */
    bool isIndeterminate() const
    {
        return totalBytes <= 0;
    }
    /*! 0.0 ~ 1.0；未知时返回 -1。 */
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
