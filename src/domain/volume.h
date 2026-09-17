/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QDateTime>
#include <QList>
#include <QMetaType>
#include <QPair>
#include <QString>

namespace Kontainer
{

/*!
 * Docker 数据卷（ARCH_V5_V8 §3.5）。
 *
 * 只读快照：`GET /volumes` 已经把详情要用的字段都给全了（驱动、挂载点、标签、选项、
 * 创建时间），因此列表与详情共用同一份数据，不再单独发 `/volumes/{name}`。
 *
 * `sizeBytes` 与 `refCount` 可能未知（`UsageData` 需要引擎真的去扫一遍，可能很慢、
 * 也可能被 `--no-usage` 类配置关掉）：**未知用 -1 表示**，界面显示"—"而不是 0
 * （"0 字节"和"不知道"是两件事）。
 */
struct Volume {
    QString name;
    /*! local / nfs / …（默认 local）。 */
    QString driver;
    /*! 宿主上的挂载点（`/var/lib/docker/volumes/<name>/_data`）。 */
    QString mountpoint;
    QDateTime createdAt;
    /*! local / global。 */
    QString scope;
    QList<QPair<QString, QString>> labels;
    QList<QPair<QString, QString>> options;
    /*! 占用字节；-1 = 引擎未提供。 */
    qint64 sizeBytes = -1;
    /*! 使用该卷的容器数；-1 = 引擎未提供。 */
    int refCount = -1;
    /*! 引擎给的补充状态文本（数据，不翻译）。 */
    QString status;

    bool isValid() const
    {
        return !name.isEmpty();
    }
    /*! 是否**确定**有容器在用（未知时返回 false，界面据此把"未知"与"未使用"分开说）。 */
    bool isInUse() const
    {
        return refCount > 0;
    }
    /*! 使用情况是否已知。 */
    bool usageKnown() const
    {
        return refCount >= 0;
    }
    /*! 大小是否已知。 */
    bool sizeKnown() const
    {
        return sizeBytes >= 0;
    }

    bool operator==(const Volume &other) const;
};

} // namespace Kontainer

Q_DECLARE_METATYPE(Kontainer::Volume)
Q_DECLARE_METATYPE(QList<Kontainer::Volume>)
