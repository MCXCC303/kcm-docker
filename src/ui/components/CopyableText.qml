/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    可复制字段行（ARCH_V3 §2.1 / ARCH_V3_pre §1.3）：

    「值 + 复制按钮」的统一实现，覆盖容器 ID、镜像仓库/标签/完整引用/镜像 ID、
    路径这类标识字段。复制动作本身在 CopyButton 里，本组件只负责排版。

    值为空时按钮禁用，而不是复制空串。

    用法（在 Kirigami.FormLayout 里直接把本组件当作标签行）：

        Components.CopyableText {
            Kirigami.FormData.label: i18n("Container ID:")
            fieldLabel: i18n("container ID")
            value: controller.shortId
            copyValue: controller.containerId   // 显示短 ID，复制完整 ID
        }
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

import "." as Local

RowLayout {
    id: control

    /*! 要展示的值。 */
    required property string value

    /*!
        实际写入剪贴板的值；为空时使用 value。
        用于「显示短 ID、复制完整 ID」这类场合：ID 太长不适合整行显示，
        但复制时必须给出完整值。
    */
    property string copyValue: ""

    /*! 无障碍与提示文案里对该字段的称呼，例如「容器 ID」。 */
    property string fieldLabel: ""

    /*! 是否等宽显示（ID / 哈希 / 路径默认开启，§1.5）。 */
    property bool monospace: true

    /*! 值为空时的占位文本。 */
    property string placeholderText: i18n("—")

    /*! 过长时的省略方式（`wrap` 为真时忽略）。 */
    property int elideMode: Text.ElideMiddle

    /*!
     * 是否换行显示（默认否，单行省略）。
     *
     * 命令这类"一句话很长、又不想丢掉中间部分"的值用换行更合适：
     * 调用方给它一个固定宽度的字段，长值就在字段内折行。
     */
    property bool wrap: false

    /*! 复制成功。 */
    signal copied

    spacing: Kirigami.Units.smallSpacing

    readonly property string effectiveCopyValue: control.copyValue.length > 0 ? control.copyValue : control.value
    readonly property bool hasValue: control.effectiveCopyValue.length > 0

    QQC2.Label {
        id: valueLabel

        objectName: "copyableTextValue"
        Layout.fillWidth: true
        text: control.value.length > 0 ? control.value : control.placeholderText
        font.family: control.monospace ? "monospace" : Kirigami.Theme.defaultFont.family
        // 省略只对真实值有意义：占位符「—」本身很短，不需要也不能被截断
        wrapMode: control.wrap ? Text.WrapAnywhere : Text.NoWrap
        elide: (control.value.length > 0 && !control.wrap) ? control.elideMode : Text.ElideNone
        opacity: control.value.length > 0 ? 1.0 : 0.6

        HoverHandler {
            id: valueHover
        }

        // 被截断时悬停显示完整值（§1.5：长仓库地址不能靠加宽卡片来解决）。
        // 这里显示的是**将被复制的完整值**，与按钮行为保持一致。
        QQC2.ToolTip.visible: valueHover.hovered && valueLabel.truncated
        QQC2.ToolTip.text: control.effectiveCopyValue
        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
    }

    Local.CopyButton {
        objectName: "copyButtonObject"
        value: control.effectiveCopyValue
        fieldLabel: control.fieldLabel
        onCopied: control.copied()
    }
}
