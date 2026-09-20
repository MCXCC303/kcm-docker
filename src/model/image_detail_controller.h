/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/docker_backend_interface.h"
#include "domain/image_detail.h"
#include "model/detail_list_model.h"

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QStringList>

namespace Kontainer
{

/*!
 * Controller for Image Detail (ARCH_V2 §8/§27/§52).
 *
 * Page entry calls start(), leaving calls stop(). Images have no dynamic metrics, so no sampling
 * timer is needed; "containers using this image" is a read-only association derived from the
 * container list.
 */
class ImageDetailController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString imageId READ imageId WRITE setImageId NOTIFY imageIdChanged)
    Q_PROPERTY(QString loadStateKey READ loadStateKey NOTIFY stateChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY stateChanged)
    Q_PROPERTY(bool hasDetail READ hasDetail NOTIFY changed)

    Q_PROPERTY(QString shortId READ shortId NOTIFY changed)
    /*! repository:tag (for the page title). */
    Q_PROPERTY(QString primaryTag READ primaryTag NOTIFY changed)
    /*! Tag name only (for the "Tag:" row). */
    Q_PROPERTY(QString tagName READ tagName NOTIFY changed)
    Q_PROPERTY(QString primaryRepository READ primaryRepository NOTIFY changed)
    Q_PROPERTY(QDateTime created READ created NOTIFY changed)
    Q_PROPERTY(qint64 sizeBytes READ sizeBytes NOTIFY changed)
    Q_PROPERTY(QString architecture READ architecture NOTIFY changed)
    Q_PROPERTY(QString os READ os NOTIFY changed)
    Q_PROPERTY(QString variant READ variant NOTIFY changed)
    Q_PROPERTY(QString author READ author NOTIFY changed)
    Q_PROPERTY(int layerCount READ layerCount NOTIFY changed)
    Q_PROPERTY(QString workingDirectory READ workingDirectory NOTIFY changed)
    Q_PROPERTY(QStringList command READ command NOTIFY changed)
    Q_PROPERTY(QStringList entrypoint READ entrypoint NOTIFY changed)
    Q_PROPERTY(int environmentCount READ environmentCount NOTIFY changed)
    Q_PROPERTY(QStringList environment READ environment NOTIFY changed)

    Q_PROPERTY(Kontainer::DetailListModel *tags READ tags CONSTANT)
    Q_PROPERTY(Kontainer::DetailListModel *digests READ digests CONSTANT)
    Q_PROPERTY(Kontainer::DetailListModel *layers READ layers CONSTANT)
    Q_PROPERTY(Kontainer::DetailListModel *usedByContainers READ usedByContainers CONSTANT)

public:
    explicit ImageDetailController(DockerBackendInterface *backend, QObject *parent = nullptr);
    ~ImageDetailController() override;

    void setImageId(const QString &id);
    QString imageId() const
    {
        return m_imageId;
    }

    QString loadStateKey() const
    {
        return m_loadStateKey;
    }
    QString errorText() const
    {
        return m_errorText;
    }
    bool hasDetail() const
    {
        return m_detail.isValid();
    }

    QString shortId() const;
    QString primaryTag() const;
    QString tagName() const;
    QString primaryRepository() const;
    QDateTime created() const
    {
        return m_detail.created;
    }
    qint64 sizeBytes() const
    {
        return m_detail.sizeBytes;
    }
    QString architecture() const
    {
        return m_detail.architecture;
    }
    QString os() const
    {
        return m_detail.os;
    }
    QString variant() const
    {
        return m_detail.variant;
    }
    QString author() const
    {
        return m_detail.author;
    }
    int layerCount() const
    {
        return int(m_detail.layers.size());
    }
    QString workingDirectory() const
    {
        return m_detail.workingDirectory;
    }
    QStringList command() const
    {
        return m_detail.command;
    }
    QStringList entrypoint() const
    {
        return m_detail.entrypoint;
    }
    int environmentCount() const
    {
        return int(m_detail.environment.size());
    }
    QStringList environment() const
    {
        return m_detail.environment;
    }

    DetailListModel *tags() const
    {
        return m_tags;
    }
    DetailListModel *digests() const
    {
        return m_digests;
    }
    DetailListModel *layers() const
    {
        return m_layers;
    }
    DetailListModel *usedByContainers() const
    {
        return m_usedBy;
    }

public Q_SLOTS:
    void start();
    void stop();
    void refresh();

Q_SIGNALS:
    void imageIdChanged();
    void stateChanged();
    void changed();

private:
    void onDetailUpdated();
    void onContainersUpdated();
    void onSectionFailed(DockerBackendInterface::Section section, const DockerError &error);
    void setLoadState(const QString &stateKey, const QString &errorText = QString());
    void rebuildLists();
    void rebuildUsedBy();

    DockerBackendInterface *m_backend = nullptr;
    DetailListModel *m_tags = nullptr;
    DetailListModel *m_digests = nullptr;
    DetailListModel *m_layers = nullptr;
    DetailListModel *m_usedBy = nullptr;

    QString m_imageId;
    QString m_loadStateKey = QStringLiteral("idle");
    QString m_errorText;
    ImageDetail m_detail;
    bool m_started = false;
};

} // namespace Kontainer
