/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>

namespace Kontainer
{

/*!
 * One image pull (ARCH_V4 §2.4).
 *
 * A pull is long-running, concurrent and keeps going in the background, so it does not fit the
 * one-result-per-operation channel: each pull keeps its own entry, which lets the UI show progress,
 * cancel a single pull, and retain the failure reason (a failed pull must never be dropped silently).
 */
struct ImagePullEntry {
    /*! Normalized reference (`alpine:latest`). */
    QString reference;
    /*! pulling / succeeded / failed / cancelled. */
    QString statusKey;
    /*! Raw engine status ("Downloading", "Pull complete", …), shown as data, never translated. */
    QString statusText;
    /*! Failure reason (raw engine text); empty while running and on success. */
    QString detailText;
    /*!
     * Failure kind key (`permissionDenied` / `timeout` / …); empty while running and on success.
     *
     * The UI uses it to tell bad credentials (401/403) from other failures: the former offers a
     * "log in…" action, the latter a retry. Matching on detail text is unreliable because engine
     * wording changes.
     */
    QString errorKindKey;
    /*! 0.0 ~ 1.0; -1 when unknown. */
    double progress = -1.0;
    bool progressKnown = false;
    int completedLayers = 0;
    int totalLayers = 0;
    /*! Still running. */
    bool active = false;

    bool isFinished() const
    {
        return !active;
    }

    friend bool operator==(const ImagePullEntry &lhs, const ImagePullEntry &rhs)
    {
        return lhs.reference == rhs.reference && lhs.statusKey == rhs.statusKey && lhs.statusText == rhs.statusText
            && lhs.detailText == rhs.detailText && lhs.errorKindKey == rhs.errorKindKey && qFuzzyCompare(lhs.progress, rhs.progress)
            && lhs.progressKnown == rhs.progressKnown && lhs.completedLayers == rhs.completedLayers
            && lhs.totalLayers == rhs.totalLayers && lhs.active == rhs.active;
    }
};

/*!
 * Pull list model (ARCH_V4 §2.4).
 *
 * Order is decided by the controller: running pulls first, finished ones after, newest end first.
 * Like the other list models: emit nothing when unchanged (progress arrives on every line, but an
 * unchanged progress must not disturb the view).
 */
class ImagePullModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY countChanged)
    Q_PROPERTY(int finishedCount READ finishedCount NOTIFY countChanged)
    Q_PROPERTY(bool empty READ empty NOTIFY countChanged)

public:
    enum Roles {
        ReferenceRole = Qt::UserRole + 1,
        StatusKeyRole,
        StatusTextRole,
        DetailTextRole,
        ProgressRole,
        ProgressKnownRole,
        CompletedLayersRole,
        TotalLayersRole,
        ActiveRole,
        ErrorKindKeyRole,
    };
    Q_ENUM(Roles)

    explicit ImagePullModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    int activeCount() const;
    int finishedCount() const;
    bool empty() const;

    void setEntries(const QList<ImagePullEntry> &entries);
    const QList<ImagePullEntry> &entries() const
    {
        return m_entries;
    }
    /*!
     * Row for a reference, -1 if absent.
     *
     * Must be Q_INVOKABLE: QML only reaches Q_INVOKABLE / slots / properties, so a plain C++
     * member is undefined there and calling it throws
     * `TypeError: Property 'rowForReference' ... is not a function`.
     */
    Q_INVOKABLE int rowForReference(const QString &reference) const;

Q_SIGNALS:
    void countChanged();

private:
    QList<ImagePullEntry> m_entries;
};

} // namespace Kontainer
