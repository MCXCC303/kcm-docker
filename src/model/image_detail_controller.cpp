/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/image_detail_controller.h"

#include "model/docker_error_text.h"
#include "model/state_text.h"

#include <KLocalizedString>

namespace Kontainer
{

using Section = DockerBackendInterface::Section;

ImageDetailController::ImageDetailController(DockerBackendInterface *backend, QObject *parent)
    : QObject(parent)
    , m_backend(backend)
    , m_tags(new DetailListModel(this))
    , m_digests(new DetailListModel(this))
    , m_layers(new DetailListModel(this))
    , m_usedBy(new DetailListModel(this))
{
    Q_ASSERT(m_backend);

    connect(m_backend, &DockerBackendInterface::imageDetailUpdated, this, &ImageDetailController::onDetailUpdated);
    connect(m_backend, &DockerBackendInterface::containersUpdated, this, &ImageDetailController::onContainersUpdated);
    connect(m_backend, &DockerBackendInterface::sectionFailed, this, &ImageDetailController::onSectionFailed);
}

ImageDetailController::~ImageDetailController() = default;

void ImageDetailController::setImageId(const QString &id)
{
    if (m_imageId == id) {
        return;
    }
    m_imageId = id;
    m_detail = ImageDetail();
    setLoadState(QStringLiteral("idle"));
    Q_EMIT imageIdChanged();
    Q_EMIT changed();
}

QString ImageDetailController::shortId() const
{
    return m_detail.shortId();
}

QString ImageDetailController::primaryTag() const
{
    const QString repository = m_detail.primaryRepository();
    const QString tag = m_detail.primaryTag();
    if (repository.isEmpty()) {
        return {};
    }
    return tag.isEmpty() ? repository : repository + QLatin1Char(':') + tag;
}

QString ImageDetailController::tagName() const
{
    return m_detail.primaryTag();
}

QString ImageDetailController::primaryRepository() const
{
    return m_detail.primaryRepository();
}

void ImageDetailController::setLoadState(const QString &stateKey, const QString &errorText)
{
    if (m_loadStateKey == stateKey && m_errorText == errorText) {
        return;
    }
    m_loadStateKey = stateKey;
    m_errorText = errorText;
    Q_EMIT stateChanged();
}

void ImageDetailController::start()
{
    m_started = true;
    if (m_imageId.isEmpty()) {
        setLoadState(QStringLiteral("error"), i18n("No image selected."));
        return;
    }
    setLoadState(QStringLiteral("loading"));
    m_backend->inspectImage(m_imageId);
}

void ImageDetailController::stop()
{
    m_started = false;
}

void ImageDetailController::refresh()
{
    if (m_imageId.isEmpty()) {
        return;
    }
    setLoadState(QStringLiteral("loading"));
    m_backend->inspectImage(m_imageId);
}

void ImageDetailController::onDetailUpdated()
{
    const ImageDetail detail = m_backend->imageDetail();
    if (detail.id != m_imageId) {
        return;
    }
    m_detail = detail;
    rebuildLists();
    rebuildUsedBy();
    setLoadState(QStringLiteral("ready"));
    Q_EMIT changed();
}

void ImageDetailController::onContainersUpdated()
{
    if (m_started && m_detail.isValid()) {
        rebuildUsedBy();
    }
}

void ImageDetailController::onSectionFailed(Section section, const DockerError &error)
{
    if (m_started && section == Section::ImageDetail) {
        setLoadState(QStringLiteral("error"), dockerErrorText(error));
    }
}

void ImageDetailController::rebuildLists()
{
    QList<DetailEntry> tags;
    tags.reserve(m_detail.repoTags.size());
    for (const QString &tag : m_detail.repoTags) {
        tags.append({tag, QString(), QString(), QStringLiteral("tag")});
    }
    m_tags->setEntries(tags);

    QList<DetailEntry> digests;
    digests.reserve(m_detail.repoDigests.size());
    for (const QString &digest : m_detail.repoDigests) {
        const int at = digest.indexOf(QLatin1Char('@'));
        if (at > 0) {
            digests.append({digest.left(at), digest.mid(at + 1), QString(), QStringLiteral("digest")});
        } else {
            digests.append({digest, QString(), QString(), QStringLiteral("digest")});
        }
    }
    m_digests->setEntries(digests);

    QList<DetailEntry> layers;
    layers.reserve(m_detail.layers.size());
    int index = 1;
    for (const QString &layer : m_detail.layers) {
        QString shortDigest = layer;
        if (shortDigest.startsWith(QLatin1String("sha256:"))) {
            shortDigest.remove(0, 7);
        }
        layers.append({QString::number(index), shortDigest.left(24), layer, QStringLiteral("layer")});
        ++index;
    }
    m_layers->setEntries(layers);
}

void ImageDetailController::rebuildUsedBy()
{
    // 只读关联：从容器列表里筛出使用该镜像的容器（§52）
    QList<DetailEntry> used;
    const QList<Container> containers = m_backend->containers();
    for (const Container &container : containers) {
        if (container.imageId != m_detail.id) {
            continue;
        }
        used.append({container.name, containerStateText(container.state), container.shortId(), container.stateKey()});
    }
    m_usedBy->setEntries(used);
}

} // namespace Kontainer
