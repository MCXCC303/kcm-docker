/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QString>

namespace Kontainer::ApiPaths
{

/*!
 * Single construction point for all Docker Engine REST paths (ARCH_V4 §2.2.1).
 *
 * A dedicated header because ARCH_V4 §1.5 asserts in a test that "REST path fragments live only
 * here", so a new endpoint needs a reviewed change rather than an ad-hoc string in some .cpp.
 * Distinct from `docker_endpoint.h`: that file describes the **connection endpoint**
 * (socket / DOCKER_HOST), this one the **request path**; don't mix them.
 *
 * DockerClient prepends the version prefix (`/v1.xx`); only version-agnostic paths live here.
 */

/* --- Unversioned: used only for version negotiation --- */
inline QString ping()
{
    return QStringLiteral("/_ping");
}
inline QString version()
{
    return QStringLiteral("/version");
}

/* --- Read-only endpoints --- */
inline QString info()
{
    return QStringLiteral("/info");
}
inline QString containersJson()
{
    return QStringLiteral("/containers/json");
}
inline QString imagesJson()
{
    return QStringLiteral("/images/json");
}
inline QString systemDf()
{
    return QStringLiteral("/system/df");
}
inline QString containerInspect(const QString &id)
{
    return QStringLiteral("/containers/%1/json").arg(id);
}
inline QString imageInspect(const QString &id)
{
    return QStringLiteral("/images/%1/json").arg(id);
}
inline QString containerStats(const QString &id)
{
    return QStringLiteral("/containers/%1/stats").arg(id);
}

/* --- Write endpoints (ARCH_V4 §2.2 / appendix A.5) --- */
inline QString containerStart(const QString &id)
{
    return QStringLiteral("/containers/%1/start").arg(id);
}
inline QString containerStop(const QString &id)
{
    return QStringLiteral("/containers/%1/stop").arg(id);
}
inline QString containerRestart(const QString &id)
{
    return QStringLiteral("/containers/%1/restart").arg(id);
}
inline QString containerRemove(const QString &id)
{
    return QStringLiteral("/containers/%1").arg(id);
}
inline QString imageCreate()
{
    return QStringLiteral("/images/create");
}
/*!
 * Network list (`GET /networks`, ARCH_V5_V8 §3.2).
 *
 * Measured: the response already holds **complete objects** (IPAM / Options / Labels /
 * Containers), so the detail page needs no second `/networks/{id}` request.
 */
inline QString networks()
{
    return QStringLiteral("/networks");
}

/*! Pause a container (`POST /containers/{id}/pause`). */
inline QString containerPause(const QString &id)
{
    return QStringLiteral("/containers/%1/pause").arg(id);
}

/*! Unpause a container (`POST /containers/{id}/unpause`). */
inline QString containerUnpause(const QString &id)
{
    return QStringLiteral("/containers/%1/unpause").arg(id);
}

/*!
 * Build an image (`POST /build`, ARCH_V5_V8 §5.3).
 *
 * The context is uploaded as a tar (`Content-Type: application/x-tar`); the response is
 * line-by-line JSON progress.
 */
inline QString buildImage()
{
    return QStringLiteral("/build");
}

/*!
 * Prune the build cache (`POST /build/prune`, ARCH_V5_V8 §5.5).
 */
inline QString buildPrune()
{
    return QStringLiteral("/build/prune");
}

/*!
 * Create a container (`POST /containers/create`, ARCH_V5_V8 §4.6).
 *
 * Note: the container name is a **query parameter** (`?name=`), not part of the request body.
 */
inline QString containerCreate()
{
    return QStringLiteral("/containers/create");
}

/*!
 * Volume list (`GET /volumes`, ARCH_V5_V8 §3.5).
 *
 * The payload is an **object** (`{Volumes, Warnings}`), unlike the `/networks` array (measured).
 */
inline QString volumes()
{
    return QStringLiteral("/volumes");
}

/*! One volume (`DELETE /volumes/{name}`; `GET /volumes/{name}` on demand). */
inline QString volume(const QString &name)
{
    return QStringLiteral("/volumes/%1").arg(name);
}

/*! Prune unused volumes (`POST /volumes/prune`, §3.5). */
inline QString volumesPrune()
{
    return QStringLiteral("/volumes/prune");
}

/*! Create a volume (`POST /volumes/create`, §3.5). */
inline QString volumeCreate()
{
    return QStringLiteral("/volumes/create");
}

/*! Create a network (`POST /networks/create`, ARCH_V5_V8 §3.3). */
inline QString networkCreate()
{
    return QStringLiteral("/networks/create");
}

/*! Remove a network (`DELETE /networks/{id}`). */
inline QString network(const QString &id)
{
    return QStringLiteral("/networks/%1").arg(id);
}

/*! Connect a container to a network (`POST /networks/{id}/connect`, §3.4). */
inline QString networkConnect(const QString &id)
{
    return QStringLiteral("/networks/%1/connect").arg(id);
}

/*! Disconnect a container from a network (`POST /networks/{id}/disconnect`, §3.4). */
inline QString networkDisconnect(const QString &id)
{
    return QStringLiteral("/networks/%1/disconnect").arg(id);
}

/*!
 * Read container logs (ARCH_V5_V8 §3.1.1).
 *
 * `stdout`/`stderr`/`follow`/`tail` all come from the query; the response is a **stream**
 * (`application/vnd.docker.raw-stream`, chunked), hence a streaming GET.
 */
inline QString containerLogs(const QString &id)
{
    return QStringLiteral("/containers/%1/logs").arg(id);
}

/*!
 * Verify registry credentials (`POST /auth`, ARCH_V5_V8 §2.6).
 *
 * Credentials go in the `X-Registry-Auth` header: with the local API 1.56 a malformed request
 * makes the engine answer `invalid X-Registry-Auth header: …`, proving it reads the header and
 * not the body. So there is neither query nor body here — credentials never appear in the URL.
 */
inline QString auth()
{
    return QStringLiteral("/auth");
}

/*!
 * Remove an image.
 *
 * `name` may be `<repo>:<tag>` (drops only that tag) or an image ID (drops the whole image; with
 * several tags the engine returns 409). Repository names may contain `/` (e.g.
 * `registry:5000/team/app:1.0`) and are **not percent-encoded**: Docker's router takes the rest
 * of that segment as the name, and encoding would make route matching fail.
 */
inline QString imageRemove(const QString &name)
{
    return QStringLiteral("/images/%1").arg(name);
}

/*!
 * Mutations introduced in phase 4.
 *
 * Each declares the minimum Engine API version it needs (the x of 1.x): on a mismatch the UI must
 * say so instead of letting the engine return an obscure error (ARCH_V3_pre §2.2). All are
 * currently ≤ 24, far below the client minimum 41, so the gate never triggers at runtime; tests
 * assert that, and a future endpoint needing more will trip it naturally.
 */
enum class Mutation {
    StartContainer,
    StopContainer,
    RestartContainer,
    RemoveContainer,
    PullImage,
    RemoveImage,
};

constexpr int requiredApiMinor(Mutation mutation)
{
    switch (mutation) {
    case Mutation::StartContainer:
    case Mutation::StopContainer:
    case Mutation::RestartContainer:
    case Mutation::RemoveContainer:
    case Mutation::PullImage:
    case Mutation::RemoveImage:
        return 24; // these endpoints have stable semantics since API 1.24
    }
    return 24;
}

} // namespace Kontainer::ApiPaths
