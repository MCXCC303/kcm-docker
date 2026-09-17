/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    拉取镜像对话框（ARCH_V4 §2.4）。

    这是一个**只负责发起**的对话框：按下「拉取」后立即关闭，拉取在后台继续，
    进度与结果都显示在镜像标签页的拉取列表里（`PullProgressList`）。

    为什么不在对话框里显示进度：拉取可能要几分钟，用户没有理由被一个模态窗口
    按在座位上；关掉对话框也不该中断拉取。同一个引用重复拉取会被拒绝并给出说明，
    不同镜像可以同时拉取。

    另外：不要写 `button.trigger()`——`QQC2.Button` 没有这个方法（那是 Action 的），
    运行时只会在按下回车时抛 TypeError（并因此什么都不做）。Enter 与按钮走同一个函数。
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

import "." as Local

Kirigami.Dialog {
    id: dialog

    required property var operations

    /*!
        引用校验与提示都交给 ImageRefInput：组件内部调用 C++ 的单一实现
        （`isValidImageReference` / `normalizedImageReference`），本文件不再重复。
    */
    readonly property bool referenceValid: referenceInput.referenceValid
    readonly property string normalizedReference: referenceInput.normalizedReference
    readonly property bool alreadyPulling: referenceInput.alreadyPulling

    signal pullRequested(string reference)

    objectName: "pullImageDialog"

    title: i18n("Pull image")
    preferredWidth: Kirigami.Units.gridUnit * 26
    padding: Kirigami.Units.largeSpacing

    function reset() {
        referenceInput.text = "";
        referenceInput.forceActiveFocus();
    }

    /*! 发起拉取（Enter 与「拉取」按钮共用这一条路径）。 */
    function startPull() {
        if (!dialog.referenceValid || dialog.alreadyPulling) {
            return;
        }
        const reference = dialog.normalizedReference;
        dialog.close();
        dialog.pullRequested(reference);
    }

    ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            Layout.fillWidth: true
            text: i18n("Image reference (for example “alpine:3.19” or “registry.example.com/team/app”).")
            wrapMode: Text.WordWrap
            font: Kirigami.Theme.smallFont
            opacity: 0.8
        }

        Local.ImageRefInput {
            id: referenceInput

            Layout.fillWidth: true
            operations: dialog.operations
            placeholderText: i18n("alpine:3.19")
            onAccepted: dialog.startPull()
        }

        QQC2.Label {
            Layout.fillWidth: true
            text: i18n("Pulling continues in the background; progress is shown in the image list.")
            wrapMode: Text.WordWrap
            font: Kirigami.Theme.smallFont
            opacity: 0.7
        }
    }

    footer: QQC2.DialogButtonBox {
        QQC2.Button {
            id: pullButton

            objectName: "pullImageButton"
            text: i18n("Pull")
            icon.name: "download"
            enabled: dialog.referenceValid && !dialog.alreadyPulling
            QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.AcceptRole
            onClicked: dialog.startPull()
        }

        QQC2.Button {
            objectName: "pullDialogCloseButton"
            text: i18n("Close")
            icon.name: "dialog-close"
            QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.RejectRole
            onClicked: dialog.close()
        }
    }
}
