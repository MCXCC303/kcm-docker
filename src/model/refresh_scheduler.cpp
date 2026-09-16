/*
    SPDX-FileCopyrightText: 2026 kontainer developers
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
    // 后台刷新请求去重由 backend 负责（§29）；这里只避免在高频数据集仍在途时堆积。
    // 注意：详情 / 采样请求在途不应拖慢列表刷新（§13.2 分层刷新）。
    if (reason == Reason::Automatic && m_backend->isRefreshingFastData()) {
        qCDebug(kontainerModel) << "skipping automatic refresh: backend busy";
        return;
    }

    m_lastAttempt = QDateTime::currentDateTimeUtc();
    m_cycleInProgress = true;
    m_cycleHadFailure = false;
    Q_EMIT stateChanged();
    m_backend->refreshAll();
}

void RefreshScheduler::onBackendLoadingChanged()
{
    if (!m_cycleInProgress || m_backend->isLoading()) {
        return; // 周期仍在进行中
    }
    m_cycleInProgress = false;
    if (m_cycleHadFailure) {
        ++m_consecutiveFailures; // 失败周期
    } else {
        m_consecutiveFailures = 0; // 完整成功周期
    }
    Q_EMIT stateChanged();
}

void RefreshScheduler::triggerStorageRefresh()
{
    if (m_backend->isRefreshingFastData()) {
        return; // storage 是中频数据，被跳过也无妨
    }
    m_backend->refreshStorageUsage();
}

void RefreshScheduler::noteFastUpdateSucceeded()
{
    // 只要有分区成功，Last Updated 就应前进（§15）；失败计数在周期结束时判定（§16）
    m_lastSuccess = QDateTime::currentDateTimeUtc();
    Q_EMIT stateChanged();
}

void RefreshScheduler::noteFastUpdateFailed()
{
    m_cycleHadFailure = true;
    Q_EMIT stateChanged();
}

} // namespace Kontainer
