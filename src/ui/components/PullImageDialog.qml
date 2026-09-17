/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    拉取镜像对话框（ARCH_V4 §2.4）。

    一个对话框承担三件事：输入引用 → 显示分层进度 → 允许取消。

    - 引用在**提交前**校验（ARCH_V3_pre §2.8 的「校验前置」）：非法输入不发往引擎
    - 未写标签时显式提示会补 latest，而不是悄悄替用户决定
    - 进度总量未知时进度条走不确定态（引擎对某些层不报 total）
    - 状态文本用引擎原文（"Downloading"、"Pull complete"…），按数据显示、不翻译，
      与容器 status（"Up 2 hours"）同等对待
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

import "." as Local

Kirigami.Dialog {
    id: dialog

    required property var operations

    /*! 归一化后实际会被拉取的引用（用于确认文案与结果提示）。 */
    readonly property string normalizedReference: operations.normalizedImageReference(referenceField.text)
    readonly property bool referenceValid: operations.isValidImageReference(referenceField.text)
    /*!
     * 用户没写标签：归一化会改变引用（例如 alpine → alpine:latest）。
     * 这种情况必须显式告诉用户会被拉取什么，而不是悄悄替用户决定。
     *
     * 注意用 trim() 而不是 trimmed()：Qt 6 的 QML JS 引擎不再把 QString 的方法
     * 挂在 JS 字符串上，写成 trimmed() 会在运行时抛 TypeError（只在求值那一刻暴露）。
     */
    readonly property bool plainReference: referenceValid
        && referenceField.text.trim() !== normalizedReference

    signal pullRequested(string reference)

    objectName: "pullImageDialog"

    title: i18n("Pull image")
    preferredWidth: Kirigami.Units.gridUnit * 26
    padding: Kirigami.Units.largeSpacing

    // 拉取进行中禁止用 Esc 关闭：取消必须走「取消拉取」按钮，
    // 否则会留下一个看不见的在途请求（ARCH_V4 §2.4）
    closePolicy: operations.pulling ? QQC2.Popup.NoAutoClose : QQC2.Popup.CloseOnEscape

    function reset() {
        referenceField.text = "";
        referenceField.forceActiveFocus();
    }

    // 内容直接作为 Dialog 的默认属性子项：Kirigami.Dialog 的 contentItem 是内部
    // Flickable，覆盖它会让滚动与宽度绑定失效。
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
            enabled: !dialog.operations.pulling
            // 只有按下拉取按钮才提交，避免输入过程中反复触发
            onAccepted: pullButton.trigger()
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

        /* --------- 进度（仅在拉取期间出现） --------- */
        Kirigami.Separator {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.smallSpacing
            visible: dialog.operations.pulling
        }

        QQC2.Label {
            Layout.fillWidth: true
            visible: dialog.operations.pulling
            text: dialog.operations.pullReference
            font.bold: true
            elide: Text.ElideMiddle
        }

        QQC2.ProgressBar {
            objectName: "pullProgressBar"
            Layout.fillWidth: true
            visible: dialog.operations.pulling
            // 总量未知时是不确定态：不能假装知道进度
            indeterminate: !dialog.operations.pullProgressKnown
            from: 0
            to: 1
            value: dialog.operations.pullProgressKnown ? dialog.operations.pullProgress : 0
        }

        QQC2.Label {
            Layout.fillWidth: true
            visible: dialog.operations.pulling
            text: {
                const parts = [];
                if (dialog.operations.pullStatusText.length > 0) {
                    parts.push(dialog.operations.pullStatusText);
                }
                if (dialog.operations.pullTotalLayers > 0) {
                    parts.push(i18n("%1 of %2 layers", dialog.operations.pullCompletedLayers, dialog.operations.pullTotalLayers));
                }
                return parts.join(" · ");
            }
            font: Kirigami.Theme.smallFont
            opacity: 0.8
            elide: Text.ElideRight
        }
    }

    footer: QQC2.DialogButtonBox {
        QQC2.Button {
            id: pullButton

            objectName: "pullImageButton"
            text: i18n("Pull")
            icon.name: "download"
            enabled: !dialog.operations.pulling && dialog.referenceValid
            QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.AcceptRole
            onClicked: dialog.pullRequested(dialog.normalizedReference)
        }

        QQC2.Button {
            objectName: "pullCancelButton"
            text: dialog.operations.pulling ? i18n("Cancel pull") : i18n("Close")
            icon.name: dialog.operations.pulling ? "process-stop" : "dialog-close"
            QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.RejectRole
            onClicked: {
                if (dialog.operations.pulling) {
                    dialog.operations.cancelPull();
                } else {
                    dialog.close();
                }
            }
        }
    }
}
