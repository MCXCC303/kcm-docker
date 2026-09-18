/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    操作结果的唯一呈现（ARCH_V4 §2.2.4）。

    成功 / 已经处于目标状态 / 取消 / 失败都从 OperationController 的结果通道读，
    页面不得各自 if-else 拼文案或选颜色：

        Components.OperationMessage {
            Layout.fillWidth: true
            operations: root.controller.operations
        }

    分级（resultCategoryKey）决定呈现强度——「用户自己能解决」用警告，
    「环境问题 / 意外」用错误，成功与取消用信息/成功，避免所有失败都长成同一条红条。
*/

import QtQuick
import QtQuick.Controls as QQC2

import org.kde.kirigami as Kirigami
import org.kde.kontainer as Kontainer

Kirigami.InlineMessage {
    id: message

    required property var operations

    readonly property bool isFailure: operations.resultCategoryKey === "userActionable"
        || operations.resultCategoryKey === "environment"
        || operations.resultCategoryKey === "unexpected"

    /*! 引擎原文（不翻译）附在摘要后面：用户报问题时它是唯一的「为什么」。 */
    readonly property string composedText: operations.resultDetailText.length > 0
        ? operations.resultText + "\n" + operations.resultDetailText
        : operations.resultText

    objectName: "operationMessage"

    /*!
     * 成功/取消类横幅的存活时间（毫秒）；0 = 不自动消失。
     *
     * 规则（用户实测反馈 A7）：**成功信息**在刷新/跳转后没有价值，也不该一直占着页面；
     * **失败信息必须留着**——它往往是用户唯一能看到的"为什么"，只能手动关闭。
     */
    property int autoDismissMs: 8000

    visible: operations.resultKey !== "none" && message.composedText.length > 0
    onVisibleChanged: {
        if (visible && !message.isFailure && message.autoDismissMs > 0) {
            autoDismissTimer.restart();
        } else if (!visible) {
            autoDismissTimer.stop();
        }
    }

    Timer {
        id: autoDismissTimer
        interval: message.autoDismissMs
        repeat: false
        onTriggered: message.operations.dismissResult()
    }
    type: {
        if (message.isFailure) {
            return operations.resultCategoryKey === "userActionable" ? Kirigami.MessageType.Warning : Kirigami.MessageType.Error;
        }
        return operations.resultKey === "cancelled" ? Kirigami.MessageType.Information : Kirigami.MessageType.Positive;
    }
    text: message.composedText
    showCloseButton: true

    actions: [
        Kirigami.Action {
            // 「目标已经消失」这类错误唯一的有效动作是把列表刷成真实状态（ARCH_V4 §2.2.2）
            visible: message.operations.resultActionKey === "refresh"
            text: i18n("Refresh")
            icon.name: "view-refresh"
            onTriggered: {
                message.operations.dismissResult();
                kcm.controller.refresh();
            }
        },
        Kirigami.Action {
            visible: message.isFailure
            text: i18n("Dismiss")
            icon.name: "dialog-close"
            onTriggered: message.operations.dismissResult()
        }
    ]

    Accessible.role: Accessible.StaticText
    Accessible.name: message.composedText
}
