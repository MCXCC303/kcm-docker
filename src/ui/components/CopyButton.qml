/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    复制按钮（ARCH_V3 §2.1）：标识类字段的复制**动作**只在这里实现一次。

    以前这段逻辑在两个详情页里手写了 6 遍（图标名、tooltip、无障碍名称、
    空值禁用、剪贴板调用），任何一处改动都要改 6 个地方。

    只用于标识字段（名称 / ID / 引用 / 路径）；仍然不提供
    「一键复制整个 inspect JSON」（ARCH_V2 §41）。

    两种用法：
      - 字段行：直接用 CopyableText（= 值 + 本按钮）
      - 页面标题这类自定义排版：单独放本按钮
*/

import QtQuick
import QtQuick.Controls as QQC2

import org.kde.kirigami as Kirigami
import org.kde.kcm.docker as Kontainer

QQC2.ToolButton {
    id: button

    /*! 要写入剪贴板的值。 */
    required property string value

    /*! 无障碍与提示文案里对该字段的称呼，例如「容器 ID」。 */
    property string fieldLabel: ""

    /*! 复制成功。 */
    signal copied

    readonly property bool hasValue: button.value.length > 0
    readonly property string actionText: button.fieldLabel.length > 0 ? i18nc("@info copy a named field", "Copy %1", button.fieldLabel) : i18n("Copy")

    icon.name: "edit-copy"
    display: QQC2.AbstractButton.IconOnly
    enabled: button.hasValue

    QQC2.ToolTip.text: button.actionText
    QQC2.ToolTip.visible: hovered
    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

    Accessible.name: button.actionText

    onClicked: {
        Kontainer.Presentation.copyToClipboard(button.value);
        button.copied();
    }
}
