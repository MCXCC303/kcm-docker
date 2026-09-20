/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
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
 * Image detail domain object (ARCH_V2 §8/§8.1).
 *
 * One image may map to several repositories/tags, so the domain assumes no single "image name"
 * and keeps repoTags / repoDigests lists; the UI decides how to merge the display.
 *
 * Layers keep only what the Docker API really provides: RootFS.Layers is a list of layer digests,
 * and the API has no per-layer size, so none is assumed here.
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
    QStringList layers; /*!< RootFS.Layers (digests) */
    QStringList environment;
    QStringList entrypoint;
    QStringList command;
    QString workingDirectory;

    bool isValid() const
    {
        return !id.isEmpty();
    }

    /*! Primary repository (repository part of the first usable tag); empty when dangling. */
    QString primaryRepository() const;
    /*! Primary tag (tag part of the first usable tag); empty when dangling. */
    QString primaryTag() const;
    /*! Short ID with the sha256: prefix removed. */
    QString shortId() const;
};

} // namespace Kontainer

Q_DECLARE_METATYPE(Kontainer::ImageDetail)
