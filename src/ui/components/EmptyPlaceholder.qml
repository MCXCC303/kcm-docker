/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    空状态占位（ARCH_V3 §2.1 / ARCH_V3_pre §1.3）：

    「没有容器」「没有挂载」「日志功能尚未提供」这类场合统一使用
    Kirigami.PlaceholderMessage，而不是留一片空白或只写一行小字。

    四态区分（无数据 / 无匹配搜索 / 无匹配过滤 / 加载失败）的**判定逻辑留在页面**
    （ARCH_V2 §33），本组件只负责呈现。

    用法：

        Components.EmptyPlaceholder {
            Layout.fillWidth: true
            message: root.containersEmptyState().message
            explanationText: ...
            actionText: root.containersEmptyState().actionText
            onActionTriggered: root.clearContainerFilters()
        }
*/

import QtQuick

import org.kde.kirigami as Kirigami

Kirigami.PlaceholderMessage {
    id: placeholder

    /*! 主文案；为空时整个占位不显示。 */
    required property string message

    /*! 补充说明；为空则不显示。 */
    property string explanationText: ""

    /*! 图标名；为空则不显示图标（避免每个空状态都顶着一个大图标）。 */
    property string iconName: ""

    /*! 引导动作文案；为空则不显示按钮。 */
    property string actionText: ""

    /*! 引导动作图标。 */
    property string actionIconName: "view-refresh"

    /*! 引导动作被触发。 */
    signal actionTriggered

    // 文案为空即整体隐藏：调用方不需要（也不应该）自己维护 visible
    visible: placeholder.message.length > 0

    text: placeholder.message
    explanation: placeholder.explanationText
    icon.name: placeholder.iconName

    readonly property Kirigami.Action placeholderAction: Kirigami.Action {
        text: placeholder.actionText
        icon.name: placeholder.actionIconName
        enabled: placeholder.actionText.length > 0
        onTriggered: placeholder.actionTriggered()
    }

    helpfulAction: placeholder.actionText.length > 0 ? placeholder.placeholderAction : null

    Accessible.role: Accessible.StaticText
    Accessible.name: placeholder.explanationText.length > 0 ? placeholder.message + ". " + placeholder.explanationText : placeholder.message
}
