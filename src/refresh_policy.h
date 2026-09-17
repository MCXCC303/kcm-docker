/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <chrono>

namespace Kontainer::RefreshPolicy
{

/*!
 * 二期刷新策略集中定义（ARCH_V2 §13.1/§13.2/§16/§21）。
 *
 * 禁止在多个 C++/QML 文件里散落 5000 / 30 / 60 这类数字：
 * 所有间隔、采样数、stale 阈值只在这里出现一次。
 */

/*! 高频：Engine 概要 + 容器列表 + 镜像列表。 */
inline constexpr std::chrono::seconds kDefaultRefreshInterval{5};

/*! 中频：Docker disk usage（/system/df 相对昂贵）。 */
inline constexpr std::chrono::seconds kStorageRefreshInterval{30};

/*! 资源采样间隔（容器详情页打开期间）。 */
inline constexpr std::chrono::seconds kStatsSampleInterval{5};

/*!
 * 详情页静态信息（inspect）的复核间隔。
 * §13.2 把 detail 归为「低频 / 页面进入时」，因此比高频的 5 秒宽得多。
 */
inline constexpr std::chrono::seconds kDetailRefreshInterval{30};

/*! 单次 HTTP 请求超时（Docker socket 本地调用，10 秒足够）。 */
inline constexpr std::chrono::seconds kRequestTimeout{10};

/*!
 * 写操作（start / stop / restart / remove / 删除镜像）的超时（ARCH_V4 §2.2.1）。
 * 比只读请求宽：引擎可能需要先做 cgroup / 文件系统操作。
 */
inline constexpr std::chrono::seconds kMutationTimeout{30};

/*!
 * `POST /containers/{id}/stop?t=` 的等待秒数：先礼貌地终止，超时才强杀。
 * 这个值由 backend 统一决定，不由 UI 传（ARCH_V4 §2.3）。
 */
inline constexpr int kStopTimeoutSeconds{10};

/*!
 * 流式请求（镜像拉取）的静默超时：多久没有新数据就算卡死。
 * 拉取本身可以合法地跑很久，因此不能设总时长上限。
 */
inline constexpr std::chrono::seconds kPullIdleTimeout{60};

/*! 资源采样连续失败多少次后停止轮询（容器可能已经停止）。 */
inline constexpr int kMaxConsecutiveStatsFailures{3};

/*! 每个详情页保留的短期采样点数：60 × 5s ≈ 5 分钟（内存中，离开页面即释放）。 */
inline constexpr int kMetricsHistorySamples{60};

/*! 连续失败达到该周期数后，已有数据被标记为 stale（§16）。 */
inline constexpr int kStaleAfterFailedCycles{2};

} // namespace Kontainer::RefreshPolicy
