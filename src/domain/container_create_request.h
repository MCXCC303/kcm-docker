/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

namespace Kontainer
{

/*!
 * One port mapping (for the create form; distinct from the read-only `PortMappingEntry`).
 *
 * `hostPort == 0` means **random assignment** (Docker picks a free port) — a common need, hence 0
 * rather than an empty string.
 */
struct ContainerPortRequest {
    QString hostIp;   /*!< empty = engine default (0.0.0.0) */
    quint16 hostPort = 0;
    quint16 containerPort = 0;
    QString protocol = QStringLiteral("tcp");
};

/*!
 * One mount request (bind / volume / tmpfs; the field meaning depends on the type).
 */
struct ContainerMountRequest {
    /*! bind / volume / tmpfs */
    QString type = QStringLiteral("bind");
    /*! Host path for bind; volume name for volume; empty for tmpfs. */
    QString source;
    /*! Path inside the container (required, absolute). */
    QString destination;
    bool readOnly = false;
};

/*!
 * Container creation request (ARCH_V5_V8 §4.4/§4.6).
 *
 * This is the **only mapping point from form fields to the Docker API**: `toJson()` is pure, so
 * "does the filled form become the right request" is pinned by snapshot assertions instead of
 * prose.
 *
 * Covers only what the phase-7 form really fills in: command/entrypoint/env/labels/ports/mounts/
 * network/restart policy/memory and CPU/privileged. The rest (devices, sysctls, health checks) is
 * added when needed rather than pre-stocked with unused fields.
 */
struct ContainerCreateRequest {
    QString name;
    QString image;
    /*! Overrides the image Cmd (empty = use the image's). */
    QStringList command;
    /*! Overrides the image Entrypoint (empty = use the image's). */
    QStringList entrypoint;
    QStringList environment; /*!< `KEY=value` form, as in the Docker API */
    QList<QPair<QString, QString>> labels;
    QString workingDirectory;
    QString user;
    QString hostname;
    /*! Ports to expose inside the container (`EXPOSE`), e.g. `80/tcp`. */
    QStringList exposedPorts;
    QList<ContainerPortRequest> ports;
    QList<ContainerMountRequest> mounts;
    /*! Network name (empty = unspecified, the engine uses the default bridge). */
    QString network;
    /*! Network aliases (valid only inside that network). */
    QStringList networkAliases;
    /*! no / always / unless-stopped / on-failure. */
    QString restartPolicy = QStringLiteral("no");
    int restartMaxRetries = 0;
    /*! Memory limit in bytes; 0 = unlimited. */
    qint64 memoryLimitBytes = 0;
    /*! CPU cores (e.g. 1.5); 0 = unlimited. */
    double cpus = 0.0;
    /*!
     * Privileged container (equivalent to host root).
     *
     * The form must confirm it twice — this is the only switch in the whole create flow that hands
     * out host root directly.
     */
    bool privileged = false;
    /*!
     * Interactivity (equivalent to `docker run -i -t`).
     *
     * Observed by users: an alpine container created with defaults "exits right after start",
     * because the image's default command (`/bin/sh`) hits EOF without stdin or a TTY. Defaulting
     * to `-i -t` is what "manually debugging a container" usually expects (and what the docker CLI
     * is normally used with).
     */
    bool openStdin = false;
    bool tty = false;
    /*! `--stdin-once`: stdin stays open only until the first client disconnects (used with `-i`). */
    bool stdinOnce = false;

    /*! Start immediately after creation (the UI's "Create and start"). */
    bool startAfterCreate = false;

    /*!
     * Request body JSON (`POST /containers/create`).
     *
     * Keys match the Docker API: `HostConfig.Binds` / `PortBindings` / `RestartPolicy` / `Memory` /
     * `NanoCpus` / `Privileged`, with networking under `NetworkingConfig.EndpointsConfig`.
     * Empty fields are left out of the JSON so the engine applies its own defaults.
     */
    QByteArray toJson() const;
    /*! Create request query (`name` is a query parameter, not a body field). */
    QString queryString() const;
};

/*!
 * Per-field validation (the validation matrix of ARCH_V5_V8 §4.3).
 *
 * Returns a **stable error key** (empty = pass); the text lives in QML, as with the project's other
 * validation. Checks that compare against existing containers (name clashes, port conflicts) belong
 * to the controllers, which have the backend data.
 */
QString validateContainerName(const QString &name);
QString validateContainerPath(const QString &path);
/*! Environment variable / label key: `[A-Za-z_][A-Za-z0-9_]*`. */
QString validateEnvironmentKey(const QString &key);
QString validateHostPort(quint16 port);
QString validateMemoryLimit(qint64 bytes);
QString validateCpus(double cpus);

} // namespace Kontainer
