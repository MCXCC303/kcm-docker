/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "domain/engine_info.h"

#include <QDateTime>
#include <QString>

namespace Kontainer
{

/*! Deployment form of the daemon (ARCH_V5_V8 §2.2). */
enum class DaemonForm {
    /*! System root daemon: config in /etc/docker/daemon.json, writes need privilege. */
    SystemRoot,
    /*! Rootless user daemon: config in ~/.config/docker/daemon.json, the user can write it. */
    Rootless,
    /*! Undetectable (incomplete engine info): display only, never write. */
    Unknown,
};

/*!
 * Deployment form and config-file state.
 *
 * The only input for the phase-5 "needs privilege?" decision: probing is **read-only**
 * (`/info` results + `QFileInfo`) and never test-writes to check permissions.
 */
struct DaemonDeployment {
    DaemonForm form = DaemonForm::Unknown;
    /*! System config path (/etc/docker/daemon.json). */
    QString systemConfigPath;
    /*! User config path (~/.config/docker/daemon.json). */
    QString userConfigPath;
    /*! Effective config path (chosen by existence and writability). */
    QString configPath;
    bool configExists = false;
    bool configWritable = false;
    qint64 configSize = 0;
    QDateTime configModified;

    /*!
     * Whether editing the config needs privilege: **only whether the current user can write this
     * file**, independent of the deployment form.
     *
     * root-owned system file → privilege; rootless user config → none.
     * The form only decides whether an edit takes effect (a separate UI banner); don't conflate.
     * With no path at all (unknown form and neither file present) claim nothing.
     */
    bool requiresPrivilege() const
    {
        return !configPath.isEmpty() && !configWritable;
    }
    /*! Data root inside the home directory ("mixed config"): worth a hint to the user. */
    bool dataRootInHomeDir = false;

    /*! Form key: systemRoot / rootless / unknown (QML switches on the key, not the enum). */
    QString formKey() const;

    bool isValid() const
    {
        return form != DaemonForm::Unknown;
    }
};

/*!
 * Detects the deployment form (ARCH_V5_V8 §2.2).
 *
 * `homeDir` and `configDirCandidates` are injectable so tests can build each form in a temp dir.
 */
class DaemonDeploymentDetector
{
public:
    /*! Detect from engine info + the real home directory. */
    static DaemonDeployment detect(const EngineInfo &info);
    /*! Injectable variant (tests). */
    static DaemonDeployment detect(const EngineInfo &info, const QString &homeDir);

    /*!
     * Whether the current user can write this config file (read-only probe, never test-write).
     *
     * Existing file → its own permission bits; absent → the **nearest existing parent** must be
     * writable (a freshly installed rootless daemon often has no `~/.config/docker/daemon.json`
     * yet, and calling that "needs root" is wrong: the user can create it).
     */
    static bool configIsWritable(const QString &path);
};

} // namespace Kontainer
