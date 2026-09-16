/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QStringList>

namespace Kontainer
{

/*!
 * 镜像 domain object。
 *
 * 一期按用户确认的范围扩展了 `GET /images/json`（ARCH_V1 §7.1 白名单之外的只读接口），
 * 仍然只读，且不涉及任何 pull/push/remove 操作。
 */
class Image
{
public:
    QString id;
    QStringList repoTags;
    QStringList repoDigests;
    qint64 sizeBytes = 0;
    QDateTime created; /*!< UTC */
    int containerCount = -1; /*!< -1 表示引擎未提供（或未使用） */
    bool inUse = false;

    bool isValid() const
    {
        return !id.isEmpty();
    }
    /*! 去掉 "sha256:" 前缀后的 12 位短 ID。 */
    QString shortId() const;
    /*! 首个仓库标签；没有标签（dangling）时返回空。 */
    QString primaryTag() const;
    bool isDangling() const;

    /*! 值比较：数据未变时不触发模型重置（ARCH_V2 §32/§34）。 */
    friend bool operator==(const Image &lhs, const Image &rhs)
    {
        return lhs.id == rhs.id && lhs.repoTags == rhs.repoTags && lhs.repoDigests == rhs.repoDigests && lhs.sizeBytes == rhs.sizeBytes
            && lhs.created == rhs.created && lhs.containerCount == rhs.containerCount && lhs.inUse == rhs.inUse;
    }
};

} // namespace Kontainer

Q_DECLARE_METATYPE(Kontainer::Image)
