/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    状态徽标（ARCH_V3 §2.1 / ARCH_V3_pre §1.3）：

    把状态统一呈现为 **图标 + 颜色 + 文字** 三重编码，
    禁止在各个页面里各自 if-else 拼颜色（颜色与徽标类型只在
    Local.StatusPalette 里映射一次）。

    用法（状态语义与图标名都来自 C++，QML 不判断状态字符串）：

        Components.StatusChip {
            semanticKey: Kontainer.Presentation.stateSemanticKey(stateKey, healthKey)
            iconName: Kontainer.Presentation.stateIconName(stateKey)
            text: stateText
        }
*/

import QtQuick

import org.kde.kirigami as Kirigami

import "." as Local

Kirigami.Badge {
    id: chip

    /*! 语义 key：positive / neutral / negative / disabled。 */
    required property string semanticKey
    /*! 图标名；为空时不显示图标（例如没有健康检查的容器）。 */
    property string iconName: ""

    type: Local.StatusPalette.badgeType(chip.semanticKey)

    // §1.6/§1.8：颜色永不单独承担语义——徽标始终带文字，图标作为第二重编码
    icon.name: chip.iconName
    icon.width: Kirigami.Units.iconSizes.sizeForLabels
    icon.height: Kirigami.Units.iconSizes.sizeForLabels

    Accessible.role: Accessible.StaticText
    Accessible.name: chip.text
    Accessible.description: chip.iconName
}
