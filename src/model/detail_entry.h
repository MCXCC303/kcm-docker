/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QString>

namespace Kontainer
{

/*!
 * 详情页里一条只读信息的展示单元（ARCH_V2 §7/§8）。
 *
 * 用于端口 / 网络 / 挂载 / 标签 / 环境变量这类“同构列表”，
 * 避免为每种列表各写一个模型，也避免把 Docker JSON 直接塞给 QML（ARCH_V1 §12.2）。
 */
struct DetailEntry {
    QString label; /*!< 主文本：网络名 / 挂载目标 / 变量名 / tag */
    QString value; /*!< 次文本：IP / 源路径 / 变量值 / digest */
    QString detail; /*!< 第三行（可选）：MAC / 模式 / 额外说明 */
    QString entryKey; /*!< 稳定 key：bind/volume/tmpfs、tcp/udp、healthy…（QML 用于图标与语义） */
    /*!
     * 关联容器的状态 key（`running` / `paused` / `exited` …）。
     *
     * "镜像关联容器"与"网络成员"两个列表都要显示**状态图标**（用户反馈：两处风格要统一），
     * 因此状态单独一个字段，不再借用 entryKey。
     */
    QString stateKey;
    /*! 点击这一行要跳转到的对象（目前只有容器 id）：空表示不可跳转。 */
    QString target;

    /*!
     * 值比较：静默刷新（数据没变）时模型不需要发任何信号（ARCH_V2 §32/§34）。
     * 详情页的列表原本每次刷新都无条件重置，导致 QML 里的 Repeater 每 5 秒
     * 销毁重建一次 delegate——这正是布局 polish 期间出现悬垂条目的温床。
     */
    friend bool operator==(const DetailEntry &lhs, const DetailEntry &rhs)
    {
        // 新字段也必须参与比较：否则"只是状态变了"不刷新（图标就不会更新）
        return lhs.label == rhs.label && lhs.value == rhs.value && lhs.detail == rhs.detail && lhs.entryKey == rhs.entryKey
            && lhs.stateKey == rhs.stateKey && lhs.target == rhs.target;
    }
};

} // namespace Kontainer
