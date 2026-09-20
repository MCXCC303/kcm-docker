/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <chrono>

namespace Kontainer::RefreshPolicy
{

/*!
 * Central definition of the phase-2 refresh policy (ARCH_V2 §13.1/§13.2/§16/§21).
 *
 * No magic numbers like 5000 / 30 / 60 scattered across C++/QML files: every interval, sample
 * count and stale threshold is defined here exactly once.
 */

/*! High frequency: engine summary + container list + image list. */
inline constexpr std::chrono::seconds kDefaultRefreshInterval{5};

/*! Medium frequency: Docker disk usage (/system/df is comparatively expensive). */
inline constexpr std::chrono::seconds kStorageRefreshInterval{30};

/*! Resource sampling interval while a container detail page is open. */
inline constexpr std::chrono::seconds kStatsSampleInterval{5};

/*!
 * Re-check interval for static detail info (inspect).
 * §13.2 classifies detail as "low frequency / on page entry", hence far wider than the 5s cadence.
 */
inline constexpr std::chrono::seconds kDetailRefreshInterval{30};

/*! Timeout for a single HTTP request (local Docker socket calls; 10s is plenty). */
inline constexpr std::chrono::seconds kRequestTimeout{10};

/*!
 * Timeout for mutations (start / stop / restart / remove / image delete) (ARCH_V4 §2.2.1).
 * Wider than read requests: the engine may need cgroup or filesystem work first.
 */
inline constexpr std::chrono::seconds kMutationTimeout{30};

/*!
 * Grace period in seconds for `POST /containers/{id}/stop?t=`: stop politely, kill on timeout.
 * Decided centrally by the backend, never passed from the UI (ARCH_V4 §2.3).
 */
inline constexpr int kStopTimeoutSeconds{10};

/*!
 * Idle timeout for streaming requests (image pull): no new data for this long means stuck.
 * A pull may legitimately run for very long, so it cannot get an overall deadline.
 */
inline constexpr std::chrono::seconds kPullIdleTimeout{60};

/*! Idle timeout while uploading the build context (phase 8 §5.1): stall tolerance for tens of MB. */
inline constexpr std::chrono::seconds kBuildUploadTimeout{120};
/*! Idle timeout on the build response stream: a build may go quiet for long (e.g. a kernel module). */
inline constexpr std::chrono::seconds kBuildIdleTimeout{300};

/*! Consecutive stats failures before polling stops (the container may have exited). */
inline constexpr int kMaxConsecutiveStatsFailures{3};

/*!
 * Watchdog cap for in-flight requests: still loading past this and we give up with a timeout
 * (seen in user testing, B3/B4).
 *
 * Set a little above the slowest normal request (reads/writes outside pull/build are tens of
 * seconds); this is a backstop, not the timeout policy — per-request idle/header timeouts do that.
 */
inline constexpr std::chrono::seconds kInFlightWatchdog{20};

/*! Short-term samples kept per detail page: 60 × 5s ≈ 5 minutes (in memory, freed on leaving). */
inline constexpr int kMetricsHistorySamples{60};

/*! After this many failed cycles, existing data is marked stale (§16). */
inline constexpr int kStaleAfterFailedCycles{2};

} // namespace Kontainer::RefreshPolicy
