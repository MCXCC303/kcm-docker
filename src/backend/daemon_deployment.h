/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "domain/engine_info.h"

#include <QDateTime>
#include <QString>

namespace Kontainer
{

/*! daemon 的部署形态（ARCH_V5_V8 §2.2）。 */
enum class DaemonForm {
    /*! 系统级 root 服务：配置文件在 /etc/docker/daemon.json，写入需要提权。 */
    SystemRoot,
    /*! 用户级 rootless daemon：配置文件在 ~/.config/docker/daemon.json，用户自己可写。 */
    Rootless,
    /*! 判断不出来（引擎信息不完整），只读展示、不提供写入。 */
    Unknown,
};

/*!
 * 部署形态与配置文件状态。
 *
 * 这是五期"要不要提权"的唯一依据：探测本身**只做只读操作**
 * （读 `/info` 的结果 + `QFileInfo`），绝不尝试写入来试探权限。
 */
struct DaemonDeployment {
    DaemonForm form = DaemonForm::Unknown;
    /*! 系统级配置路径（/etc/docker/daemon.json）。 */
    QString systemConfigPath;
    /*! 用户级配置路径（~/.config/docker/daemon.json）。 */
    QString userConfigPath;
    /*! 实际生效的配置路径（按存在性与可写性选择）。 */
    QString configPath;
    bool configExists = false;
    bool configWritable = false;
    qint64 configSize = 0;
    QDateTime configModified;

    /*!
     * 是否需要提权才能修改配置：**只看这个文件当前用户能不能写**，与部署形态无关。
     *
     * 系统级文件属于 root → 需要提权；rootless 的用户配置 → 不需要。
     * 形态只决定"改了会不会生效"（界面上的另一条横幅），两件事不能混。
     * 连路径都没有（形态未知且两个文件都不存在）时不做任何提权声明。
     */
    bool requiresPrivilege() const
    {
        return !configPath.isEmpty() && !configWritable;
    }
    /*! 数据目录落在用户家目录（"混合配置"）：值得给用户一条提示。 */
    bool dataRootInHomeDir = false;

    /*! 形态 key：systemRoot / rootless / unknown（QML 不判断枚举，只判断 key）。 */
    QString formKey() const;

    bool isValid() const
    {
        return form != DaemonForm::Unknown;
    }
};

/*!
 * 探测部署形态（ARCH_V5_V8 §2.2）。
 *
 * `homeDir` 与 `configDirCandidates` 可注入，便于测试在临时目录里构造各种形态。
 */
class DaemonDeploymentDetector
{
public:
    /*! 用引擎信息 + 真实家目录探测。 */
    static DaemonDeployment detect(const EngineInfo &info);
    /*! 可注入版本（测试用）。 */
    static DaemonDeployment detect(const EngineInfo &info, const QString &homeDir);

    /*!
     * 当前用户能否写这个配置文件（只读探测，绝不试写）。
     *
     * 文件存在 → 看它自己的权限位；不存在 → 看**最近的已存在父目录**能不能写
     * （新装的 rootless daemon 往往还没有 `~/.config/docker/daemon.json`，
     * 这时把它判成"需要 root"是错的：用户明明可以创建它）。
     */
    static bool configIsWritable(const QString &path);
};

} // namespace Kontainer
