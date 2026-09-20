/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "domain/image_build.h"

#include <QAbstractListModel>
#include <QList>

namespace Kontainer
{

/*!
 * One entry in the build list (ARCH_V5_V8 §5.3).
 *
 * Same pattern as pull: builds are long-running, concurrent, cancellable, and must not lose the
 * failure reason, so they never reuse the one-result-per-operation notification channel.
 */
struct ImageBuildEntry {
    QString id;
    /*! Context directory (reused when building again). */
    QString contextDirectory;
    /*! Tags (display only; may be several). */
    QStringList tags;
    /*! building / succeeded / failed / cancelled. */
    QString statusKey = QStringLiteral("building");
    /*! Raw engine status (`Step 3/7 : RUN make`), shown as data and never translated. */
    QString statusText;
    /*! Step info parsed out of `statusText`. */
    int stepIndex = 0;
    int totalSteps = 0;
    QString stepCommand;
    /*! Failure reason (includes the failing step, assembled from raw engine output). */
    QString detailText;
    QString errorKindKey;
    /*! 0.0 ~ 1.0 (derived from step counts); -1 when unknown. */
    double progress = -1.0;
    bool progressKnown = false;
    /*! Image id once the build succeeded. */
    QString imageId;
    bool active = true;

    friend bool operator==(const ImageBuildEntry &lhs, const ImageBuildEntry &rhs)
    {
        return lhs.id == rhs.id && lhs.statusKey == rhs.statusKey && lhs.statusText == rhs.statusText
            && lhs.stepIndex == rhs.stepIndex && lhs.totalSteps == rhs.totalSteps && lhs.stepCommand == rhs.stepCommand
            && lhs.detailText == rhs.detailText && lhs.errorKindKey == rhs.errorKindKey
            && qFuzzyCompare(lhs.progress, rhs.progress) && lhs.progressKnown == rhs.progressKnown
            && lhs.imageId == rhs.imageId && lhs.active == rhs.active;
    }
};

/*!
 * Build list model.
 *
 * Like the pull list: emit nothing when the content is unchanged (progress updates on every line,
 * but duplicate lines must not disturb the view).
 */
class ImageBuildModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY countChanged)
    Q_PROPERTY(int finishedCount READ finishedCount NOTIFY countChanged)
    Q_PROPERTY(bool empty READ empty NOTIFY countChanged)

public:
    enum Roles {
        BuildIdRole = Qt::UserRole + 1,
        TagsRole,
        StatusKeyRole,
        StatusTextRole,
        StepIndexRole,
        TotalStepsRole,
        StepCommandRole,
        DetailTextRole,
        ProgressRole,
        ProgressKnownRole,
        ImageIdRole,
        ActiveRole,
        ErrorKindKeyRole,
    };
    Q_ENUM(Roles)

    explicit ImageBuildModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    int activeCount() const;
    int finishedCount() const;
    bool empty() const;

    void setEntries(const QList<ImageBuildEntry> &entries);
    const QList<ImageBuildEntry> &entries() const
    {
        return m_entries;
    }
    /*! Row for a build id, -1 if absent (Q_INVOKABLE: QML can only call exposed functions). */
    Q_INVOKABLE int rowForBuildId(const QString &buildId) const;

Q_SIGNALS:
    void countChanged();

private:
    QList<ImageBuildEntry> m_entries;
};

} // namespace Kontainer
