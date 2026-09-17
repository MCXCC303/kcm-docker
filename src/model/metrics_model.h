/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "backend/docker_backend_interface.h"
#include "domain/container_stats.h"

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QVariantList>

class QTimer;

namespace Kontainer
{

/*! 一次采样在 presentation 层的形态（百分比与速率已经算好，QML 不做数学，§18）。 */
struct MetricsPoint {
    QDateTime timestamp;
    double cpuPercent = 0.0;
    quint64 memoryUsedBytes = 0;
    quint64 memoryLimitBytes = 0;
    double memoryPercent = -1.0; /*!< < 0 表示没有有效 limit */
    double networkRxPerSecond = -1.0; /*!< < 0 表示尚不可计算（首个采样/计数器回绕） */
    double networkTxPerSecond = -1.0;
    double blockReadPerSecond = -1.0;
    double blockWritePerSecond = -1.0;
};

/*!
 * 容器资源指标模型（ARCH_V2 §17–§22/§27/§44）。
 *
 * - 只保留内存中的短期环形缓冲（默认 60 × 5s ≈ 5 分钟），不写磁盘、不做历史数据库（§21）
 * - 速率由相邻采样差分得到；首个采样与计数器回绕都标记为“未知”（-1）而不是编造数字
 * - 页面离开时 stop()：停止采样并释放历史（§27）
 */
class MetricsModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool sampling READ sampling NOTIFY stateChanged)
    Q_PROPERTY(bool hasData READ hasData NOTIFY updated)
    Q_PROPERTY(double cpuPercent READ cpuPercent NOTIFY updated)
    Q_PROPERTY(qint64 memoryUsedBytes READ memoryUsedBytes NOTIFY updated)
    Q_PROPERTY(qint64 memoryLimitBytes READ memoryLimitBytes NOTIFY updated)
    Q_PROPERTY(double memoryPercent READ memoryPercent NOTIFY updated)
    /*! 是否存在“真实”的内存上限（等于宿主内存视为无限制，§19）。 */
    Q_PROPERTY(bool memoryLimitEffective READ memoryLimitEffective NOTIFY updated)
    Q_PROPERTY(double networkRxPerSecond READ networkRxPerSecond NOTIFY updated)
    Q_PROPERTY(double networkTxPerSecond READ networkTxPerSecond NOTIFY updated)
    Q_PROPERTY(double blockReadPerSecond READ blockReadPerSecond NOTIFY updated)
    Q_PROPERTY(double blockWritePerSecond READ blockWritePerSecond NOTIFY updated)
    Q_PROPERTY(int sampleCount READ sampleCount NOTIFY updated)
    /*!
     * 环形缓冲容量（= RefreshPolicy::kMetricsHistorySamples）。
     *
     * 趋势图按固定条数渲染（不够的槽位留空），这样 Repeater 的 model 永远不变，
     * 采样时不会销毁/创建任何柱子——这是 ARCH_V3 附录 A.1g 那条崩溃路径的根治办法。
     */
    Q_PROPERTY(int historyCapacity READ historyCapacity CONSTANT)
    Q_PROPERTY(QVariantList cpuHistory READ cpuHistory NOTIFY updated)
    Q_PROPERTY(QVariantList networkHistory READ networkHistory NOTIFY updated)
    Q_PROPERTY(QVariantList memoryHistory READ memoryHistory NOTIFY updated)

public:
    explicit MetricsModel(QObject *parent = nullptr);

    void setBackend(DockerBackendInterface *backend);

    /*! 开始对指定容器采样（进入详情页）。 */
    void start(const QString &containerId);
    /*! 停止采样并释放历史（离开详情页）。 */
    void stop();

    bool sampling() const
    {
        return m_sampling;
    }
    bool hasData() const
    {
        return !m_points.isEmpty();
    }
    QString containerId() const
    {
        return m_containerId;
    }

    double cpuPercent() const;
    qint64 memoryUsedBytes() const;
    qint64 memoryLimitBytes() const;
    double memoryPercent() const;
    bool memoryLimitEffective() const;
    double networkRxPerSecond() const;
    double networkTxPerSecond() const;
    double blockReadPerSecond() const;
    double blockWritePerSecond() const;
    int historyCapacity() const;
    int sampleCount() const;
    QVariantList cpuHistory() const;
    QVariantList networkHistory() const;
    QVariantList memoryHistory() const;

    /*! 由 controller 在收到新的原始采样时调用。 */
    void addSample(const ContainerStats &stats);
    /*! 采样定时器触发（间隔来自 RefreshPolicy，不散落 magic number）。 */
    void requestSample();
    /*! 采样失败：用于停止对已经停止/消失的容器的轮询。 */
    void noteFailure();

Q_SIGNALS:
    void stateChanged();
    void updated();

private:
    MetricsPoint pointFrom(const ContainerStats &stats) const;
    bool memoryLimitEffectiveFor(quint64 limit) const;
    QVariantList series(bool cpu, bool network) const;

    DockerBackendInterface *m_backend = nullptr;
    QTimer *m_timer = nullptr;
    QString m_containerId;
    bool m_sampling = false;
    QList<MetricsPoint> m_points;
    ContainerStats m_previous;
    bool m_hasPrevious = false;
    int m_consecutiveFailures = 0;
};

} // namespace Kontainer
