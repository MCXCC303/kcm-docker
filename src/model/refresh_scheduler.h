/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/docker_backend_interface.h"

#include <QDateTime>
#include <QObject>

class QTimer;

namespace Kontainer
{

/*!
 * Refresh scheduling (ARCH_V2 §13/§14/§15/§16).
 *
 * Responsibilities:
 *  - own the page-level timers (fast data set + medium-frequency storage), with intervals defined
 *    centrally in RefreshPolicy
 *  - track the last refresh attempt and the last successful update (§15)
 *  - count consecutive failed cycles and decide staleness (§16)
 *
 * Not responsible for: HTTP, JSON, UI text; it does not know which page the user is on either (§43).
 * The detail page's stats sampling is managed by MetricsModel itself (life cycle tied to the page, §27).
 */
class RefreshScheduler : public QObject
{
    Q_OBJECT

public:
    enum class Reason {
        Initial,
        Automatic,
        Manual,
    };
    Q_ENUM(Reason)

    explicit RefreshScheduler(DockerBackendInterface *backend, QObject *parent = nullptr);

    void setAutoRefreshEnabled(bool enabled);
    bool autoRefreshEnabled() const
    {
        return m_autoRefreshEnabled;
    }

    /*! Trigger a refresh of the fast data set (engine + containers + images). */
    void requestRefresh(Reason reason);

    /*! Fast data set updated successfully (controllers call this on *Updated). */
    void noteFastUpdateSucceeded();
    /*! Fast data set failed (controllers call this on sectionFailed). */
    void noteFastUpdateFailed();
    /*!
     * Failure counting for one refresh cycle (§16): the cycle counts as failed if any fast section
     * failed, and the counter only resets after a fully clean cycle — otherwise "containers failed
     * + images succeeded" within one refresh would wipe the count.
     */
    void onBackendLoadingChanged();

    QDateTime lastAttempt() const
    {
        return m_lastAttempt;
    }
    QDateTime lastSuccess() const
    {
        return m_lastSuccess;
    }
    int consecutiveFailures() const
    {
        return m_consecutiveFailures;
    }
    /*! Failures reached the threshold after at least one success: cached data is stale (§16). */
    bool isStale() const;

    int refreshIntervalMs() const;
    int storageIntervalMs() const;

Q_SIGNALS:
    void stateChanged();
    void autoRefreshEnabledChanged();

private:
    void triggerStorageRefresh();

    DockerBackendInterface *m_backend = nullptr;
    QTimer *m_fastTimer = nullptr;
    QTimer *m_storageTimer = nullptr;

    bool m_autoRefreshEnabled = true;
    bool m_cycleInProgress = false;
    bool m_cycleHadFailure = false;
    QDateTime m_lastAttempt;
    QDateTime m_lastSuccess;
    int m_consecutiveFailures = 0;
};

} // namespace Kontainer
