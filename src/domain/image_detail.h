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
 * 镜像详情 domain object（ARCH_V2 §8/§8.1）。
 *
 * 一个 image 可能对应多个 repository/tag，因此 domain 不假设
 * “一个 image 名称”，而是保留 repoTags / repoDigests 列表，由 UI 决定如何合并展示。
 *
 * Layer 信息只保留 Docker API 实际提供的内容：RootFS.Layers 是层 digest 列表，
 * API 不提供单层大小，因此这里不做假设。
 */
struct ImageDetail {
    QString id;
    QStringList repoTags;
    QStringList repoDigests;
    QDateTime created;
    qint64 sizeBytes = 0;
    QString architecture;
    QString os;
    QString variant;
    QString author;
    QStringList layers; /*!< RootFS.Layers（digest） */
    QStringList environment;
    QStringList entrypoint;
    QStringList command;
    QString workingDirectory;

    bool isValid() const
    {
        return !id.isEmpty();
    }

    /*! 主仓库名（第一个可用 tag 的 repository 部分），dangling 时为空。 */
    QString primaryRepository() const;
    /*! 主 tag（第一个可用 tag 的 tag 部分），dangling 时为空。 */
    QString primaryTag() const;
    /*! 去掉 sha256: 前缀的短 ID。 */
    QString shortId() const;
};

} // namespace Kontainer

Q_DECLARE_METATYPE(Kontainer::ImageDetail)
