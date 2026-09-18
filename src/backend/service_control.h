/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QString>
#include <QStringList>

namespace Kontainer
{

/*!
 * 对 systemd 服务能做的动作（ARCH_V5_V8 §B1）。
 *
 * 只有这五个：**固定动词**，不接受任意 systemctl 参数。
 */
enum class ServiceVerb {
    Start,
    Stop,
    Restart,
    Enable,
    Disable,
};

/*! 稳定 key（界面与测试都用它，不传枚举）→ `start` / `stop` / `restart` / `enable` / `disable`。 */
QString serviceVerbKey(ServiceVerb verb);
/*! key → 枚举；不认识时返回 false（调用方据此拒绝）。 */
bool serviceVerbFromKey(const QString &key, ServiceVerb *verb);
/*! 全部动词（界面铺按钮、测试遍历用）。 */
QStringList managedServiceVerbs();

/*! 该 unit 是否在我们管理的白名单里（`managedServiceUnits()`）。 */
bool isManagedServiceUnit(const QString &unit);

/*! 动词对应的 KAuth 动作名（`org.kde.kontainer.service.start`）。 */
QString serviceActionName(ServiceVerb verb);
/*! 动词对应的 helper 槽名（`service_start`）。 */
QString serviceHelperSlot(ServiceVerb verb);

/*!
 * 校验一个"控制服务"的请求，返回**稳定错误 key**（空 = 通过）。
 *
 * 这是提权边界上最重要的一段逻辑，因此是纯函数、可单测：
 *  - `unitNotManaged`：不在白名单里的 unit（拒绝任意 unit 名）
 *  - `verbNotManaged`：不认识的动词
 *  - `unitRequired` / `verbRequired`：空值
 *
 * 会话侧与 helper 侧都会调用它（纵深防御）：即使会话侧被绕过，
 * helper 也不会执行白名单之外的任何东西。
 */
QString serviceControlArgumentError(const QString &unit, const QString &verbKey);

} // namespace Kontainer
