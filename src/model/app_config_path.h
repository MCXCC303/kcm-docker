/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QString>

namespace Kontainer
{

/*!
 * 本模块的配置文件路径（`~/.config/kcm_dockerrc`），并做一次**旧名字迁移**。
 *
 * 软件从 `kontainer` 更名为 `kcm-docker`（原名已被其它应用占用），配置文件也跟着改名；
 * 但用户已有的挂载预设与命令历史不能因此丢掉，所以首次运行时把旧文件**复制**过来
 * （复制而不是移动：旧文件留着，万一用户回退旧版本也还能用）。
 *
 * @param configDir 配置目录；留空时用 `QStandardPaths` 的标准位置（测试传临时目录）。
 */
QString defaultAppConfigPath(const QString &configDir = QString());

} // namespace Kontainer
