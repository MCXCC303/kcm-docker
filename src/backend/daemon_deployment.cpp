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

bool DaemonDeploymentDetector::configIsWritable(const QString &path)
{
    if (path.isEmpty()) {
        return false;
    }
    const QFileInfo info(path);
    if (info.exists()) {
        // 已存在的文件：只信它自己的权限位（父目录可写但文件只读 = 不能改）
        return info.isWritable();
    }
    // 还不存在：能创建就算可写。向上找到最近的已存在目录来判断，
    // 因为 ~/.config/docker 可能整条链都还没建
    // （注意不能用 QDir::cdUp()：它要求目标目录已存在，正好是这里要处理的情况）
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

    // 配置路径：形态已知时直接用该形态的路径；形态未知时只在用户配置确实存在时采信它。
    // 不知道是哪个 daemon 就绝不猜系统路径——按猜测往 /etc 写是不可接受的
    // （界面本来就会按作用域给出路径，这里只是"探测出来的默认值"）
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

    // 数据目录在家目录里：常见于"系统级 daemon + 用户家目录数据"的混合配置，
    // 值得在界面上点出来（用户容易误以为是 rootless）
    deployment.dataRootInHomeDir = pathIsInside(info.dockerRootDir, homeDir);
    return deployment;
}

} // namespace Kontainer
