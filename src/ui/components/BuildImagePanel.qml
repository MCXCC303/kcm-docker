/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Build list and build form (ARCH_V5_V8 §5.3/§5.4).

    Same pattern as the pull list: builds are long-running, several can run at once, and
    the failure reason must not be lost. So progress does not go into a modal dialog:

      - running: progress bar (derived from step counts, indeterminate when unknown) +
        the engine's own status text + step command + cancel
      - finished: success (offers "show image") / cancelled / **failure stays with its
        failing step**
      - several finished records can be cleared in one click

    The form is an **inline panel**, not a dialog: phase 6 measured popup content as
    unreliable under offscreen/never-shown timings, and this form has many fields (context
    directory, Dockerfile, tags, build arguments, target) — inline is steadier and handier.

    Usage:

        Components.BuildImagePanel {
            Layout.fillWidth: true
            operations: root.operations
        }
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.kcm.docker as Kontainer

import "." as Local

ColumnLayout {
    id: root

    required property var operations
    /*! Bytes currently held by the build cache (0 = nothing reclaimable; passed in by the caller). */
    property real buildCacheBytes: 0

    /*! "Show image" clicked after a successful build. */
    signal imageRequested(string imageId)

    objectName: "buildImagePanel"

    /* ---------------- Form state ---------------- */
    property bool formOpen: false
    property string contextDirectory: ""
    property string dockerfile: "Dockerfile"
    property bool inlineDockerfileEnabled: false
    property string inlineDockerfile: ""
    property string tagsText: ""
    property string target: ""
    property bool noCache: false
    property bool pullBase: false

    /*! Stable key of the form validation (empty = submittable). */
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

    /*! Validation key → text. */
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

    /* ---------------------------- Form ---------------------------- */
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
                    // This is a Dockerfile syntax sample (code), not UI copy
                    placeholderText: "FROM alpine:3.19\nRUN echo hello" // i18n-lint: allow Dockerfile syntax sample
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

        }
    }

    /*! Build arguments (written back by the editor). */
    property var buildArgs: []

    /*! Submit: handed to the controller (validation and packing live there; the UI only collects). */
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

    /*! Build arguments need only the value list (the editor returns `{key, value}` pairs). */
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

    /* --------------------- Build cache cleanup (§5.5) --------------------- */
    RowLayout {
        objectName: "buildPruneRow"
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            Layout.fillWidth: true
            // Reclaimable space comes from the build-cache section of /system/df: **look before deleting**
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

    /* ---------------------------- List ---------------------------- */
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

                    // After success, go straight to the image (the most natural next step from this list)
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
                    // An unknown step count means indeterminate: do not pretend to know the progress
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
                            // The failing step is in detailText (backend-built "Step N/M (CMD) failed: …")
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
