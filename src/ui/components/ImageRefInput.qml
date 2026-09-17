/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    镜像引用输入（ARCH_V5_V8 §1.6）。

    拉取镜像、容器创建、Dockerfile 构建的目标 tag 都用它——**校验只有一份实现**：
    组件自己不写正则，而是调用 C++ 侧的 `isValidImageReference` /
    `normalizedImageReference`（四期就在 `ImageReference` 里实现并测过）。

    用法：

        Components.ImageRefInput {
            operations: root.operations        // 提供两个校验方法
            placeholderText: i18n("alpine:3.19")
            onAccepted: root.startPull(text)
        }

    组件的对外面：`text`（可读可写）、`referenceValid`、`normalizedReference`、
    `plainReference`（缺 tag 会补 latest）、`alreadyPulling`、信号 `accepted`。
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

import "." as Local

ColumnLayout {
    id: root

    objectName: "imageRefInput"

    /*! 提供 `isValidImageReference` / `normalizedImageReference` / `pulls` 的对象。 */
    required property var operations

    /*! 当前输入文本（调用方读写它）。 */
    property alias text: field.text
    /*! 输入框占位文本。 */
    property string placeholderText: i18n("alpine:3.19")
    /*! 是否显示"已经在拉取"的提示（只有拉取场景需要）。 */
    property bool checkAlreadyPulling: true

    /*! 文本是否是合法的镜像引用。 */
    readonly property bool referenceValid: root.operations.isValidImageReference(root.text)
    /*! 当前输入对应的仓库地址（无效引用时为空）：拉取对话框据此提示"还没登录"。 */
    readonly property string serverAddress: root.referenceValid ? root.operations.serverAddressForImage(root.text) : ""
    /*! 归一化后的引用（缺 tag 会补 `latest`）。 */
    readonly property string normalizedReference: root.operations.normalizedImageReference(root.text)
    /*!
     * 归一化改变了引用（用户没写 tag）→ 必须显式告知会被拉取什么。
     * 注意用 `trim()`：Qt 6 的 QML JS 不再把 QString 方法挂在字符串上（`trimmed()` 会抛 TypeError）。
     */
    readonly property bool plainReference: root.referenceValid && root.text.trim() !== root.normalizedReference
    /*!
     * 该引用已经在进行中的拉取列表里。
     *
     * 注意 `count > 0` 不是多余的：`rowForReference` 是 Q_INVOKABLE，QML 不会追踪函数调用，
     * 必须同时读一个可通知属性（列表条数）才能让这个绑定在拉取开始/结束时重新求值。
     */
    readonly property bool alreadyPulling: root.checkAlreadyPulling && root.referenceValid
        && root.operations.pulls.count > 0
        && root.operations.pulls.rowForReference(root.normalizedReference) >= 0
    /*! 是否可以提交（合法且没有重复拉取）。 */
    readonly property bool acceptable: root.referenceValid && !root.alreadyPulling

    /*! 用户按下回车（或调用 `forceActiveFocus()` 后确认）。 */
    signal accepted

    spacing: Kirigami.Units.smallSpacing

    function forceActiveFocus() {
        field.forceActiveFocus();
    }

    QQC2.TextField {
        id: field

        objectName: "imageRefField"
        Layout.fillWidth: true
        placeholderText: root.placeholderText
        onAccepted: root.accepted()
    }

    QQC2.Label {
        objectName: "imageRefError"
        Layout.fillWidth: true
        visible: field.text.length > 0 && !root.referenceValid
        text: i18n("This is not a valid image reference.")
        // 负面色只能经 StatusPalette 取（状态色单一来源，§12）
        color: Local.StatusPalette.color("negative")
        wrapMode: Text.WordWrap
        font: Kirigami.Theme.smallFont
    }

    QQC2.Label {
        objectName: "imageRefLatestHint"
        Layout.fillWidth: true
        visible: root.referenceValid && root.plainReference
        text: i18n("No tag given, “latest” will be pulled: %1", root.normalizedReference)
        wrapMode: Text.WordWrap
        font: Kirigami.Theme.smallFont
        opacity: 0.8
    }

    QQC2.Label {
        objectName: "imageRefAlreadyPullingHint"
        Layout.fillWidth: true
        visible: root.alreadyPulling
        text: i18n("This image is already being pulled.")
        wrapMode: Text.WordWrap
        font: Kirigami.Theme.smallFont
        opacity: 0.8
    }

    Accessible.name: i18n("Image reference")
    Accessible.description: root.text
}
