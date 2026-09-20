/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backend/daemon_deployment.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace Kontainer
{

namespace
{
constexpr auto kSystemConfigPath = "/etc/docker/daemon.json";

QString userConfigPathFor(const QString &homeDir)
{
    return homeDir + QStringLiteral("/.config/docker/daemon.json");
}

bool pathIsInside(const QString &path, const QString &directory)
{
    if (path.isEmpty() || directory.isEmpty()) {
        return false;
    }
    const QString normalizedPath = QDir::cleanPath(path);
    const QString normalizedDir = QDir::cleanPath(directory);
    return normalizedPath.startsWith(normalizedDir + QLatin1Char('/'));
}
} // namespace

QString DaemonDeployment::formKey() const
{
    switch (form) {
    case DaemonForm::SystemRoot:
        return QStringLiteral("systemRoot");
    case DaemonForm::Rootless:
        return QStringLiteral("rootless");
    case DaemonForm::Unknown:
        break;
    }
    return QStringLiteral("unknown");
}

DaemonDeployment DaemonDeploymentDetector::detect(const EngineInfo &info)
{
    return detect(info, QStandardPaths::writableLocation(QStandardPaths::HomeLocation));
}

bool DaemonDeploymentDetector::configIsWritable(const QString &path)
{
    if (path.isEmpty()) {
        return false;
    }
    const QFileInfo info(path);
    if (info.exists()) {
        // Existing file: trust its own permission bits (writable parent + read-only file = no edit)
        return info.isWritable();
    }
    // Absent: creatable counts as writable. Walk up to the nearest existing directory, since the
    // whole ~/.config/docker chain may be missing.
    // (Not QDir::cdUp(): it requires the target to exist, exactly the case handled here.)
    QString dir = info.absolutePath();
    while (!dir.isEmpty() && !QFileInfo::exists(dir)) {
        const QString parent = QFileInfo(dir).absolutePath();
        if (parent == dir) {
            break;
        }
        dir = parent;
    }
    return !dir.isEmpty() && QFileInfo(dir).isWritable();
}

DaemonDeployment DaemonDeploymentDetector::detect(const EngineInfo &info, const QString &homeDir)
{
    DaemonDeployment deployment;
    deployment.systemConfigPath = QString::fromLatin1(kSystemConfigPath);
    deployment.userConfigPath = userConfigPathFor(homeDir);

    // Form comes only from `SecurityOptions`: a rootless daemon carries `name=rootless`
    bool rootless = false;
    for (const QString &option : info.securityOptions) {
        if (option.contains(QLatin1String("rootless"))) {
            rootless = true;
            break;
        }
    }
    if (rootless) {
        deployment.form = DaemonForm::Rootless;
    } else if (!info.securityOptions.isEmpty()) {
        // SecurityOptions present but no rootless → system root daemon
        deployment.form = DaemonForm::SystemRoot;
    }

    // Config path: use the form's path when the form is known; when unknown, trust the user config
    // only if it exists. Never guess the system path — a guessed write to /etc is unacceptable
    // (the UI offers a path per scope anyway; this is just the detected default).
    const QFileInfo userInfo(deployment.userConfigPath);

    QString chosen;
    if (deployment.form == DaemonForm::Rootless) {
        chosen = deployment.userConfigPath;
    } else if (deployment.form == DaemonForm::SystemRoot) {
        chosen = deployment.systemConfigPath;
    } else if (userInfo.exists()) {
        chosen = deployment.userConfigPath;
    }

    deployment.configPath = chosen;
    const QFileInfo chosenInfo(chosen);
    deployment.configExists = chosenInfo.exists();
    deployment.configWritable = configIsWritable(chosen);
    deployment.configSize = deployment.configExists ? chosenInfo.size() : 0;
    deployment.configModified = deployment.configExists ? chosenInfo.lastModified() : QDateTime();

    // Data root inside $HOME: typical of "system daemon + user home data", worth calling out in
    // the UI (users easily mistake it for rootless)
    deployment.dataRootInHomeDir = pathIsInside(info.dockerRootDir, homeDir);
    return deployment;
}

} // namespace Kontainer
