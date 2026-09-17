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

    /*! 输入框里的文本是否可用作镜像引用。 */
    readonly property bool referenceValid: operations.isValidImageReference(referenceField.text)
    /*! 归一化后实际会被拉取的引用。 */
    readonly property string normalizedReference: operations.normalizedImageReference(referenceField.text)
    /*!
     * 用户没写标签：归一化会改变引用（例如 alpine → alpine:latest）。
     * 必须显式告诉用户会被拉取什么，而不是悄悄替用户决定。
     */
    readonly property bool plainReference: referenceValid && referenceField.text.trim() !== normalizedReference
    /*! 这个引用已经在拉了：给出提示，避免重复请求（引擎侧也会去重）。 */
    readonly property bool alreadyPulling: referenceValid && operations.pulls.rowForReference(normalizedReference) >= 0

    signal pullRequested(string reference)

    objectName: "pullImageDialog"

    title: i18n("Pull image")
    preferredWidth: Kirigami.Units.gridUnit * 26
    padding: Kirigami.Units.largeSpacing

    function reset() {
        referenceField.text = "";
        referenceField.forceActiveFocus();
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

        QQC2.TextField {
            id: referenceField

            objectName: "pullReferenceField"
            Layout.fillWidth: true
            placeholderText: i18n("alpine:3.19")
            onAccepted: dialog.startPull()
        }

        QQC2.Label {
            Layout.fillWidth: true
            visible: referenceField.text.length > 0 && !dialog.referenceValid
            text: i18n("This is not a valid image reference.")
            // 负面色只能经 StatusPalette 取（状态色单一来源，§12）
            color: Local.StatusPalette.color("negative")
            wrapMode: Text.WordWrap
            font: Kirigami.Theme.smallFont
        }

        QQC2.Label {
            Layout.fillWidth: true
            visible: dialog.referenceValid && dialog.plainReference
            text: i18n("No tag given, “latest” will be pulled: %1", dialog.normalizedReference)
            wrapMode: Text.WordWrap
            font: Kirigami.Theme.smallFont
            opacity: 0.8
        }

        QQC2.Label {
            objectName: "alreadyPullingHint"
            Layout.fillWidth: true
            visible: dialog.alreadyPulling
            text: i18n("This image is already being pulled.")
            wrapMode: Text.WordWrap
            font: Kirigami.Theme.smallFont
            opacity: 0.8
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
