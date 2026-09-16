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
 * Docker disk usage（`GET /system/df`）的 Engine 级统计（ARCH_V2 §23/§24）。
 *
 * 数据来源是结构化 API，不是 `docker system df` CLI（§24）。
 * 各类别可能缺失（取决于 API 版本与是否有 build cache），全部按可选值处理：
 * `available == false` 表示该类别不可用，UI 应显示 “—” 而不是 0。
 */
struct StorageUsage {
    bool valid = false; /*!< 至少成功解析了响应 */
    bool buildCacheAvailable = false; /*!< BuildCache 字段是否存在 */

    qint64 imagesBytes = -1;
    qint64 containersBytes = -1;
    qint64 volumesBytes = -1;
    qint64 buildCacheBytes = -1;
    qint64 layersBytes = -1; /*!< 所有 image layer 的总大小（可能大于三者之和） */

    int imageCount = 0;
    int containerCount = 0;
    int volumeCount = 0;
    int buildCacheCount = 0;

    /*! 已知类别的合计；缺失的类别按 0 计。 */
    qint64 totalBytes() const;
};

} // namespace Kontainer

Q_DECLARE_METATYPE(Kontainer::StorageUsage)
