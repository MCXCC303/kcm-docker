/*
    SPDX-FileCopyrightText: 2026 kontainer developers
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

DaemonDeployment DaemonDeploymentDetector::detect(const EngineInfo &info, const QString &homeDir)
{
    DaemonDeployment deployment;
    deployment.systemConfigPath = QString::fromLatin1(kSystemConfigPath);
    deployment.userConfigPath = userConfigPathFor(homeDir);

    // 形态判定只看 `SecurityOptions`：rootless daemon 会带 `name=rootless`
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
        // 有 SecurityOptions 但不含 rootless → 系统级 root daemon
        deployment.form = DaemonForm::SystemRoot;
    }

    // 配置路径：形态已知时优先该形态的路径；否则按"存在 → 可写"的顺序挑
    const QFileInfo systemInfo(deployment.systemConfigPath);
    const QFileInfo userInfo(deployment.userConfigPath);

    QString chosen;
    if (deployment.form == DaemonForm::Rootless) {
        chosen = deployment.userConfigPath;
    } else if (deployment.form == DaemonForm::SystemRoot) {
        chosen = deployment.systemConfigPath;
    } else if (userInfo.exists()) {
        chosen = deployment.userConfigPath;
    } else if (systemInfo.exists()) {
        chosen = deployment.systemConfigPath;
    }

    deployment.configPath = chosen;
    const QFileInfo chosenInfo(chosen);
    deployment.configExists = chosenInfo.exists();
    deployment.configWritable = deployment.configExists && chosenInfo.isWritable();
    deployment.configSize = deployment.configExists ? chosenInfo.size() : 0;
    deployment.configModified = deployment.configExists ? chosenInfo.lastModified() : QDateTime();

    // 数据目录在家目录里：常见于"系统级 daemon + 用户家目录数据"的混合配置，
    // 值得在界面上点出来（用户容易误以为是 rootless）
    deployment.dataRootInHomeDir = pathIsInside(info.dockerRootDir, homeDir);
    return deployment;
}

} // namespace Kontainer
