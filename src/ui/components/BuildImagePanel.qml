/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    构建列表与构建表单（ARCH_V5_V8 §5.3/§5.4）。

    与拉取列表同一个模式：构建是长任务、可以同时跑多个、失败原因不能丢。
    因此进度不进模态框，而是：

      - 进行中：进度条（按步骤数推算，未知时不确定态）+ 引擎状态原文 + 步骤命令 + 取消
      - 已结束：成功（给「查看镜像详情」）/ 已取消 / **失败带着失败的步骤一直留着**
      - 多个已完成记录可以一键清空

    表单做成**内联面板**而不是对话框：六期实测过弹层内容在离屏/未展示时序下不可靠，
    这里的字段又多（上下文目录、Dockerfile、标签、构建参数、target），内联更稳也更好用。

    用法：

        Components.BuildImagePanel {
            Layout.fillWidth: true
            operations: root.operations
        }
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.kontainer as Kontainer

import "." as Local

ColumnLayout {
    id: root

    required property var operations
    /*! 构建缓存当前占用的字节数（0 = 没有可回收的；由使用方从存储用量传入）。 */
    property real buildCacheBytes: 0

    /*! 构建成功后点「查看镜像详情」。 */
    signal imageRequested(string imageId)

    objectName: "buildImagePanel"

    /* ---------------- 表单状态 ---------------- */
    property bool formOpen: false
    property string contextDirectory: ""
    property string dockerfile: "Dockerfile"
    property bool inlineDockerfileEnabled: false
    property string inlineDockerfile: ""
    property string tagsText: ""
    property string target: ""
    property bool noCache: false
    property bool pullBase: false

    /*! 表单校验的稳定 key（空 = 可以提交）。 */
    readonly property string formErrorKey: {
        if (root.contextDirectory.trim().length === 0) {
            return "contextRequired";
        }
        if (!root.contextDirectory.trim().startsWith("/")) {
            return "contextNotAbsolute";
        }
        if (root.tagsText.trim().length === 0) {
            return "tagRequired";
        }
        return "";
    }

    /*! 校验 key → 文案。 */
    function errorText(key: string): string {
        switch (key) {
        case "contextRequired":
            return i18n("Choose the directory that contains the Dockerfile.");
        case "contextNotAbsolute":
            return i18n("The build context must be an absolute path.");
        case "tagRequired":
            return i18n("Give the image at least one tag, for example app:1.0.");
        case "contextMissing":
            return i18n("This directory does not exist.");
        case "notADirectory":
            return i18n("The build context must be a directory.");
        case "dockerfileMissing":
            return i18n("There is no Dockerfile in this directory. Paste one below, or pick another directory.");
        case "contextTooLarge":
            return i18n("The build context is too large. Check .dockerignore.");
        case "tooManyFiles":
            return i18n("The build context contains too many files. Check .dockerignore.");
        default:
            return "";
        }
    }

    readonly property var tagList: {
        const result = [];
        for (const part of root.tagsText.split("\n")) {
            const trimmed = part.trim();
            if (trimmed.length > 0) {
                result.push(trimmed);
            }
        }
        return result;
    }

    spacing: Kirigami.Units.smallSpacing

    /* ---------------------------- 表单 ---------------------------- */
    Kirigami.AbstractCard {
        objectName: "buildImageForm"
        Layout.fillWidth: true
        visible: root.formOpen
        showClickFeedback: false

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            Kirigami.Heading {
                Layout.fillWidth: true
                level: 3
                text: i18n("Build an image from a Dockerfile")
            }

            Kirigami.InlineMessage {
                objectName: "buildFormError"
                Layout.fillWidth: true
                visible: root.formErrorKey.length > 0 && root.contextDirectory.length > 0
                type: Kirigami.MessageType.Error
                text: root.errorText(root.formErrorKey)
            }

            Kirigami.InlineMessage {
                objectName: "buildFormOperationError"
                Layout.fillWidth: true
                visible: root.operations.resultKey === "error" && root.operations.resultText.length > 0
                type: Kirigami.MessageType.Error
                text: root.operations.resultText
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                QQC2.TextField {
                    objectName: "buildContextField"
                    Layout.fillWidth: true
                    placeholderText: i18n("Build context directory, for example /home/me/project")
                    Accessible.name: i18n("Build context directory")
                    text: root.contextDirectory
                    onTextChanged: root.contextDirectory = text
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                QQC2.TextField {
                    objectName: "buildDockerfileField"
                    Layout.fillWidth: true
                    enabled: !root.inlineDockerfileEnabled
                    placeholderText: i18n("Dockerfile name")
                    Accessible.name: i18n("Dockerfile name")
                    text: root.dockerfile
                    onTextChanged: root.dockerfile = text
                }

                QQC2.CheckBox {
                    objectName: "buildInlineDockerfileCheck"
                    text: i18n("Paste the Dockerfile instead")
                    checked: root.inlineDockerfileEnabled
                    onToggled: root.inlineDockerfileEnabled = checked
                }
            }

            QQC2.ScrollView {
                objectName: "buildInlineDockerfileArea"
                Layout.fillWidth: true
                Layout.preferredHeight: visible ? Kirigami.Units.gridUnit * 8 : 0
                visible: root.inlineDockerfileEnabled
                clip: true

                QQC2.TextArea {
                    objectName: "buildInlineDockerfileField"
                    // 这是 Dockerfile 语法示例（代码），不是界面文案
                    placeholderText: "FROM alpine:3.19\nRUN echo hello" // i18n-lint: allow Dockerfile 语法示例
                    Accessible.name: i18n("Dockerfile content")
                    font.family: "monospace"
                    wrapMode: TextEdit.NoWrap
                    text: root.inlineDockerfile
                    onTextChanged: root.inlineDockerfile = text
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                QQC2.TextField {
                    objectName: "buildTagsField"
                    Layout.fillWidth: true
                    placeholderText: i18n("Image tags, one per line (app:1.0)")
                    Accessible.name: i18n("Image tags")
                    text: root.tagsText
                    onTextChanged: root.tagsText = text
                }
                QQC2.TextField {
                    objectName: "buildTargetField"
                    Layout.fillWidth: true
                    placeholderText: i18n("Target stage (optional)")
                    Accessible.name: i18n("Target stage")
                    text: root.target
                    onTextChanged: root.target = text
                }
            }

            QQC2.Label {
                Layout.fillWidth: true
                text: i18n("Build arguments:")
                font.bold: true
            }

            Local.KeyValueListEditor {
                id: buildArgsEditor

                objectName: "buildArgsEditor"
                Layout.fillWidth: true
                onChanged: root.buildArgs = buildArgsEditor.entries()
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                QQC2.CheckBox {
                    objectName: "buildNoCacheCheck"
                    text: i18n("Do not use the build cache")
                    checked: root.noCache
                    onToggled: root.noCache = checked
                }
                QQC2.CheckBox {
                    objectName: "buildPullCheck"
                    text: i18n("Always pull the base image")
                    checked: root.pullBase
                    onToggled: root.pullBase = checked
                }

                Item {
                    Layout.fillWidth: true
                }

                QQC2.Button {
                    objectName: "buildCancelFormButton"
                    text: i18n("Cancel")
                    onClicked: root.formOpen = false
                }

                QQC2.Button {
                    objectName: "buildStartButton"
                    text: i18n("Build")
                    icon.name: "system-run"
                    enabled: root.formErrorKey.length === 0 && root.operations.writeAllowed
                    onClicked: root.submit()
                }
            }

            QQC2.Label {
                Layout.fillWidth: true
                text: i18n("The context is packed locally; .dockerignore is respected, and links pointing outside the directory are skipped.")
                font: Kirigami.Theme.smallFont
                opacity: 0.7
                wrapMode: Text.WordWrap
            }
        }
    }

    /*! 构建参数（由编辑器回写）。 */
    property var buildArgs: []

    /*! 提交：交给控制器（校验与打包在那边，界面只负责收集字段）。 */
    function submit(): void {
        if (root.formErrorKey.length > 0) {
            return;
        }
        const started = root.operations.buildImage(root.contextDirectory.trim(),
                                                   root.tagList,
                                                   root.inlineDockerfileEnabled ? "Dockerfile" : root.dockerfile.trim(),
                                                   root.buildArgsKeys(),
                                                   root.buildArgs,
                                                   root.target.trim(),
                                                   root.noCache,
                                                   root.pullBase,
                                                   root.inlineDockerfileEnabled ? root.inlineDockerfile : "");
        if (started) {
            root.formOpen = false;
            root.contextDirectory = "";
            root.tagsText = "";
            root.target = "";
            root.inlineDockerfile = "";
            root.inlineDockerfileEnabled = false;
            root.noCache = false;
            root.pullBase = false;
        }
    }

    /*! 构建参数只需要值列表（键值对编辑器给的是 `{key, value}`）。 */
    function buildArgsKeys(): var {
        const result = [];
        for (const entry of root.buildArgs) {
            const key = (entry.key ?? "").trim();
            if (key.length > 0) {
                result.push(key + "=" + (entry.value ?? ""));
            }
        }
        return result;
    }

    /* --------------------- 构建缓存清理（§5.5） --------------------- */
    RowLayout {
        objectName: "buildPruneRow"
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            Layout.fillWidth: true
            // 可回收空间取自 /system/df 的构建缓存段：**先看清楚再删**
            text: root.buildCacheBytes > 0
                ? i18n("Build cache: %1 can be reclaimed. Untagged intermediate images are removed.", Kontainer.Format.byteSize(root.buildCacheBytes))
                : i18n("Build cache: nothing to reclaim right now.")
            font: Kirigami.Theme.smallFont
            opacity: 0.75
            wrapMode: Text.WordWrap
        }

        QQC2.Button {
            objectName: "pruneBuildCacheButton"
            text: i18n("Clean up build cache…")
            icon.name: "edit-clear-history"
            enabled: root.operations.writeAllowed && root.buildCacheBytes > 0
            onClicked: pruneBuildCacheDialog.open()
        }
    }

    Local.ConfirmDialog {
        id: pruneBuildCacheDialog

        objectName: "pruneBuildCacheDialog"
        headingText: i18n("Clean up the build cache")
        questionText: i18n("Remove cached build layers?")
        consequenceText: i18n("The next build has to redo the work that was cached. Images are not affected; only intermediate layers and cache records are removed.")
        acceptText: i18n("Clean up")
        destructive: true
        onConfirmed: root.operations.pruneBuildCache()
    }

    /* ---------------------------- 列表 ---------------------------- */
    Kirigami.Heading {
        Layout.fillWidth: true
        visible: root.operations.builds.count > 0
        level: 3
        text: root.operations.builds.activeCount > 0
            ? i18n("Building images (%1)", root.operations.builds.activeCount)
            : i18n("Recent builds")
    }

    QQC2.Button {
        objectName: "clearFinishedBuildsButton"
        Layout.alignment: Qt.AlignRight
        visible: root.operations.builds.finishedCount > 0
        text: i18n("Clear finished")
        icon.name: "edit-clear-all"
        flat: true
        onClicked: root.operations.clearFinishedBuilds()
    }

    Repeater {
        model: root.operations.builds

        delegate: Kirigami.AbstractCard {
            id: buildCard

            required property string buildId
            required property var tags
            required property string statusKey
            required property string statusText
            required property int stepIndex
            required property int totalSteps
            required property string stepCommand
            required property string detailText
            required property double progress
            required property bool progressKnown
            required property string imageId
            required property bool active

            objectName: "buildEntry"
            Layout.fillWidth: true
            showClickFeedback: false

            readonly property bool failed: buildCard.statusKey === "failed"
            readonly property bool succeeded: buildCard.statusKey === "succeeded"
            readonly property bool cancelled: buildCard.statusKey === "cancelled"

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing / 2

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Icon {
                        source: buildCard.failed ? "dialog-error" : buildCard.succeeded ? "dialog-ok-apply" : "run-build"
                        color: buildCard.failed ? Local.StatusPalette.color("negative")
                            : buildCard.succeeded ? Local.StatusPalette.color("positive")
                            : Kirigami.Theme.textColor
                        implicitWidth: Kirigami.Units.iconSizes.smallMedium
                        implicitHeight: Kirigami.Units.iconSizes.smallMedium
                    }

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: buildCard.tags.join(", ")
                        font.bold: true
                        elide: Text.ElideMiddle
                    }

                    QQC2.BusyIndicator {
                        objectName: "buildBusyIndicator"
                        visible: buildCard.active
                        running: buildCard.active
                        implicitWidth: Kirigami.Units.iconSizes.smallMedium
                        implicitHeight: Kirigami.Units.iconSizes.smallMedium
                    }

                    QQC2.Button {
                        objectName: "cancelBuildButton"
                        visible: buildCard.active
                        text: i18n("Cancel")
                        icon.name: "process-stop"
                        flat: true
                        onClicked: root.operations.cancelBuild(buildCard.buildId)
                    }

                    // 成功后可以直接去看这个镜像（构建列表里最自然的下一步）
                    QQC2.Button {
                        objectName: "buildOpenImageButton"
                        visible: buildCard.succeeded && buildCard.imageId.length > 0
                        text: i18n("Show image")
                        icon.name: "document-open"
                        flat: true
                        onClicked: root.imageRequested(buildCard.imageId)
                    }
                }

                QQC2.ProgressBar {
                    objectName: "buildProgressBar"
                    Layout.fillWidth: true
                    visible: buildCard.active
                    // 步数未知时是不确定态：不能假装知道进度
                    indeterminate: !buildCard.progressKnown
                    from: 0
                    to: 1
                    value: buildCard.progressKnown ? buildCard.progress : 0
                }

                QQC2.Label {
                    objectName: "buildStatusLabel"
                    Layout.fillWidth: true
                    text: {
                        if (buildCard.failed) {
                            // 失败的步骤在 detailText 里（后端拼好的 Step N/M (CMD) failed: …）
                            return buildCard.detailText.length > 0
                                ? i18n("Build failed: %1", buildCard.detailText)
                                : i18n("The build failed.");
                        }
                        if (buildCard.succeeded) {
                            return i18n("Image built.");
                        }
                        if (buildCard.cancelled) {
                            return i18n("Build cancelled.");
                        }
                        const parts = [];
                        if (buildCard.totalSteps > 0) {
                            parts.push(i18n("Step %1 of %2", buildCard.stepIndex, buildCard.totalSteps));
                        }
                        if (buildCard.stepCommand.length > 0) {
                            parts.push(buildCard.stepCommand);
                        } else if (buildCard.statusText.length > 0) {
                            parts.push(buildCard.statusText);
                        }
                        return parts.join(" · ");
                    }
                    color: buildCard.failed ? Local.StatusPalette.color("negative") : Kirigami.Theme.textColor
                    opacity: buildCard.active ? 0.85 : 1.0
                    wrapMode: Text.WordWrap
                    elide: Text.ElideRight
                    font: Kirigami.Theme.smallFont
                }
            }

            Accessible.name: buildCard.failed ? i18n("Build failed: %1", buildCard.detailText) : buildCard.tags.join(", ")
        }
    }

    QQC2.Label {
        objectName: "buildListEmptyHint"
        Layout.fillWidth: true
        visible: root.operations.builds.count === 0 && !root.formOpen
        text: i18n("Builds you start stay in this list, so you can keep working while one runs.")
        font: Kirigami.Theme.smallFont
        opacity: 0.7
        wrapMode: Text.WordWrap
    }
}
