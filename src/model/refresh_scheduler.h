/*
    SPDX-FileCopyrightText: 2026 kontainer developers
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
 * 刷新调度（ARCH_V2 §13/§14/§15/§16）。
 *
 * 职责：
 *  - 持有页面级定时器（高频数据集 + 中频 storage），间隔来自 RefreshPolicy 集中定义
 *  - 记录最近一次刷新尝试时间与最近一次成功更新时间（§15）
 *  - 统计连续失败周期数并给出 stale 判定（§16）
 *
 * 不负责：HTTP、JSON、UI 文本；也不知道用户在哪个页面（§43）。
 * 详情页的 stats 采样节奏由 MetricsModel 自己管理（生命周期与页面绑定，§27）。
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

    /*! 触发一次高频数据集刷新（引擎 + 容器 + 镜像）。 */
    void requestRefresh(Reason reason);

    /*! 高频数据集成功更新（由 controller 在 *Updated 时调用）。 */
    void noteFastUpdateSucceeded();
    /*! 高频数据集失败（由 controller 在 sectionFailed 时调用）。 */
    void noteFastUpdateFailed();
    /*!
     * 一个刷新周期的失败计数语义（§16）：
     * 周期内只要有一个高频分区失败，就记为一次失败周期；只有整周期无失败才清零。
     * 否则同一次刷新里 “containers 失败 + images 成功” 会把失败计数抹掉。
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
    /*! 连续失败达到阈值且曾经成功过：已有数据已过期（§16）。 */
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
