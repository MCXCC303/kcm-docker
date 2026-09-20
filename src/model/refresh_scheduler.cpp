/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "model/refresh_scheduler.h"

#include "logging.h"
#include "refresh_policy.h"

#include <QTimer>

namespace Kontainer
{

namespace
{
int toMilliseconds(std::chrono::seconds interval)
{
    return int(std::chrono::duration_cast<std::chrono::milliseconds>(interval).count());
}
} // namespace

RefreshScheduler::RefreshScheduler(DockerBackendInterface *backend, QObject *parent)
    : QObject(parent)
    , m_backend(backend)
    , m_fastTimer(new QTimer(this))
    , m_storageTimer(new QTimer(this))
{
    Q_ASSERT(m_backend);

    m_fastTimer->setInterval(toMilliseconds(RefreshPolicy::kDefaultRefreshInterval));
    connect(m_fastTimer, &QTimer::timeout, this, [this] {
        requestRefresh(Reason::Automatic);
    });

    m_storageTimer->setInterval(toMilliseconds(RefreshPolicy::kStorageRefreshInterval));
    connect(m_storageTimer, &QTimer::timeout, this, &RefreshScheduler::triggerStorageRefresh);

    m_fastTimer->start();
    m_storageTimer->start();

    connect(m_backend, &DockerBackendInterface::loadingChanged, this, &RefreshScheduler::onBackendLoadingChanged);
}

int RefreshScheduler::refreshIntervalMs() const
{
    return m_fastTimer->interval();
}

int RefreshScheduler::storageIntervalMs() const
{
    return m_storageTimer->interval();
}

bool RefreshScheduler::isStale() const
{
    return m_lastSuccess.isValid() && m_consecutiveFailures >= RefreshPolicy::kStaleAfterFailedCycles;
}

void RefreshScheduler::setAutoRefreshEnabled(bool enabled)
{
    if (m_autoRefreshEnabled == enabled) {
        return;
    }
    m_autoRefreshEnabled = enabled;
    if (enabled) {
        m_fastTimer->start();
        m_storageTimer->start();
    } else {
        m_fastTimer->stop();
        m_storageTimer->stop();
    }
    Q_EMIT autoRefreshEnabledChanged();
}

void RefreshScheduler::requestRefresh(Reason reason)
{
    // the backend deduplicates refresh requests (§29); this only avoids pile-ups while fast data is in flight
    // note: in-flight detail/sampling requests must not slow list refreshes (§13.2 layered refresh).
    if (reason == Reason::Automatic && m_backend->isRefreshingFastData()) {
        qCDebug(kontainerModel) << "skipping automatic refresh: backend busy";
        return;
    }

    m_lastAttempt = QDateTime::currentDateTimeUtc();
    m_cycleInProgress = true;
    m_cycleHadFailure = false;
    if (reason == Reason::Manual) {
        /*
         * A manual refresh is a fresh start: clear the previous round's failure count.
         *
         * User report B2: after stopping and restarting the service the data was in fact updated,
         * yet the UI kept showing "update failed" — the counter only resets after a fully clean
         * cycle, and the first automatic refresh after recovery may be interrupted again. The manual
         * path must clear it explicitly so the state matches reality.
         */
        m_consecutiveFailures = 0;
    }
    Q_EMIT stateChanged();
    m_backend->refreshAll();
}

void RefreshScheduler::onBackendLoadingChanged()
{
    if (!m_cycleInProgress || m_backend->isLoading()) {
        return; // the cycle is still running
    }
    m_cycleInProgress = false;
    if (m_cycleHadFailure) {
        ++m_consecutiveFailures; // failed cycle
    } else {
        m_consecutiveFailures = 0; // fully successful cycle
    }
    Q_EMIT stateChanged();
}

void RefreshScheduler::triggerStorageRefresh()
{
    if (m_backend->isRefreshingFastData()) {
        return; // storage is medium frequency, skipping it is harmless
    }
    m_backend->refreshStorageUsage();
}

void RefreshScheduler::noteFastUpdateSucceeded()
{
    // any successful section advances Last Updated (§15); failures are judged at the cycle's end (§16)
    m_lastSuccess = QDateTime::currentDateTimeUtc();
    Q_EMIT stateChanged();
}

void RefreshScheduler::noteFastUpdateFailed()
{
    m_cycleHadFailure = true;
    Q_EMIT stateChanged();
}

} // namespace Kontainer
