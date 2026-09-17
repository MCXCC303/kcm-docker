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

    /*! 是否需要提权才能修改配置。 */
    bool requiresPrivilege() const
    {
        return form == DaemonForm::SystemRoot && !configWritable;
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
};

} // namespace Kontainer
