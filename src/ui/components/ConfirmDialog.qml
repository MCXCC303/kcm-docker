/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    确认对话框（ARCH_V3_pre §1.3 / ARCH_V4 §2.2.5）。

    破坏性操作（删除容器 / 删除镜像）必须走这里，并且**句式固定**：

        标题：<操作名>
        正文：确定要 <动作> <目标类型>「<名称>」吗？ <后果说明>

    为什么不做成每个操作自己拼一套对话框：破坏性操作的文案质量直接决定用户
    会不会误删东西，「后果说明」必须是必填参数，不能被漏掉。start / stop /
    restart 是可逆操作，不做二次确认（ARCH_V4 §四）。

    用法：

        Components.ConfirmDialog {
            id: removeDialog
            headingText: i18n("Delete container")
            questionText: i18n("Delete the container “%1”?", page.controller.name)
            consequenceText: i18n("The container is removed; its volumes are kept.")
            acceptText: i18n("Delete")
            destructive: true
            onConfirmed: page.controller.operations.removeContainer(page.containerId)
        }
*/

import QtQuick

import org.kde.kirigami as Kirigami

Kirigami.PromptDialog {
    id: dialog

    /*! 标题（操作名，例如「删除容器」）。 */
    required property string headingText
    /*! 「确定要 … 吗？」这句话本身（由调用方填入目标名称）。 */
    required property string questionText
    /*!
     * 后果说明（必填）：用户点下去会发生什么。
     * 例如「容器会被删除；它的匿名卷与命名卷不会被删除。」
     */
    required property string consequenceText
    /*! 确认按钮文案（用动词，而不是「确定」）。 */
    property string acceptText: i18n("Continue")
    /*! 破坏性操作：使用警告样式与删除图标。 */
    property bool destructive: false

    signal confirmed

    title: dialog.headingText
    subtitle: dialog.consequenceText.length > 0 ? dialog.questionText + " " + dialog.consequenceText : dialog.questionText
    dialogType: dialog.destructive ? Kirigami.PromptDialog.Warning : Kirigami.PromptDialog.Information

    // 用 customFooterActions 而不是 standardButtons：按钮上必须写清楚动作
    // （「删除」而不是「OK」），这本身也是防误操作的一部分。
    standardButtons: Kirigami.Dialog.NoButton
    customFooterActions: [
        Kirigami.Action {
            text: dialog.acceptText
            icon.name: dialog.destructive ? "edit-delete" : "dialog-ok"
            onTriggered: {
                dialog.close();
                dialog.confirmed();
            }
        },
        Kirigami.Action {
            text: i18n("Cancel")
            icon.name: "dialog-cancel"
            onTriggered: dialog.close()
        }
    ]

    // 注意：Accessible 是**附加属性**，只能挂在 Item / Action 上。
    // 挂在 Kirigami.PromptDialog（不是 Item）上会在运行时报
    // "Accessible attached property must be attached to an object deriving from Item or Action"，
    // 而且标题/正文本来就由对话框自己以可访问文本暴露，这里不需要再加。
}
