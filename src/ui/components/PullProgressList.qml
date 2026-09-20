/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Pull list (ARCH_V4 §2.4).

    Pulling is a long task: several different images can run at once, and closing the pull
    dialog does not interrupt them. So progress lives here rather than in the modal dialog:

      - active: progress bar (indeterminate while the total is unknown) + raw engine status
        + layer count + cancel
      - finished: success / cancelled / failure; **a failure stays with its raw engine text**
        until the user dismisses it, so "pull failed" is never swallowed silently
      - several finished entries can be cleared in one go

    Usage:

        Components.PullProgressList {
            Layout.fillWidth: true
            operations: root.operations
        }
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

import "." as Local

ColumnLayout {
    id: root

    required property var operations

    /*! Credential-related failure (engine returned 401/403): guide the user to log in instead of retrying. */
    signal loginRequested(string reference)

    objectName: "pullProgressList"
    visible: root.operations.pulls.count > 0
    spacing: Kirigami.Units.smallSpacing

    Kirigami.Heading {
        Layout.fillWidth: true
        level: 3
        text: root.operations.pulls.activeCount > 0
            ? i18n("Pulling images (%1)", root.operations.pulls.activeCount)
            : i18n("Recent pulls")
    }

    // "Clear finished" only appears when there really are finished entries
    QQC2.Button {
        objectName: "clearFinishedPullsButton"
        Layout.alignment: Qt.AlignRight
        visible: root.operations.pulls.finishedCount > 0
        text: i18n("Clear finished")
        icon.name: "edit-clear-all"
        flat: true
        onClicked: root.operations.clearFinishedPulls()
    }

    Repeater {
        model: root.operations.pulls

        delegate: Kirigami.AbstractCard {
            id: pullCard

            required property string reference
            required property string statusKey
            required property string statusText
            required property string detailText
            required property double progress
            required property bool progressKnown
            required property int completedLayers
            required property int totalLayers
            required property bool active
            /*! Failure kind key (`permissionDenied` offers "Log in…"). */
            required property string errorKindKey

            objectName: "pullEntry"
            Layout.fillWidth: true
            showClickFeedback: false

            readonly property bool failed: pullCard.statusKey === "failed"
            readonly property bool succeeded: pullCard.statusKey === "succeeded"
            readonly property bool cancelled: pullCard.statusKey === "cancelled"

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing / 2

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Icon {
                        source: pullCard.failed ? "dialog-error" : pullCard.succeeded ? "dialog-ok-apply" : "download"
                        color: pullCard.failed ? Local.StatusPalette.color("negative")
                            : pullCard.succeeded ? Local.StatusPalette.color("positive")
                            : Kirigami.Theme.textColor
                        implicitWidth: Kirigami.Units.iconSizes.smallMedium
                        implicitHeight: Kirigami.Units.iconSizes.smallMedium
                    }

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: pullCard.reference
                        font.bold: true
                        elide: Text.ElideMiddle
                    }

                    QQC2.BusyIndicator {
                        objectName: "pullBusyIndicator"
                        visible: pullCard.active
                        running: pullCard.active
                        implicitWidth: Kirigami.Units.iconSizes.smallMedium
                        implicitHeight: Kirigami.Units.iconSizes.smallMedium
                    }

                    // Active: this one pull can be cancelled (other images keep pulling)
                    QQC2.Button {
                        objectName: "cancelPullButton"
                        visible: pullCard.active
                        text: i18n("Cancel")
                        icon.name: "process-stop"
                        flat: true
                        onClicked: root.operations.cancelPull(pullCard.reference)
                    }

                    // Credential-related failure (engine returned 401/403): log in first instead of retrying
                    QQC2.Button {
                        objectName: "pullLoginButton"
                        visible: !pullCard.active && pullCard.statusKey === "failed" && pullCard.errorKindKey === "permissionDenied"
                        text: i18n("Log in…")
                        icon.name: "dialog-password"
                        flat: true
                        onClicked: root.loginRequested(pullCard.reference)
                    }

                    // Finished: remove this entry (failures too must be dismissed explicitly by the user)
                    QQC2.Button {
                        objectName: "dismissPullButton"
                        visible: !pullCard.active
                        icon.name: "edit-clear"
                        flat: true
                        Accessible.name: i18n("Remove this entry from the list")
                        QQC2.ToolTip.text: i18n("Remove this entry")
                        QQC2.ToolTip.visible: hovered
                        onClicked: root.operations.dismissPull(pullCard.reference)
                    }
                }

                QQC2.ProgressBar {
                    objectName: "pullProgressBar"
                    Layout.fillWidth: true
                    visible: pullCard.active
                    // Indeterminate while the total is unknown: we must not pretend to know the progress
                    indeterminate: !pullCard.progressKnown
                    from: 0
                    to: 1
                    value: pullCard.progressKnown ? pullCard.progress : 0
                }

                QQC2.Label {
                    objectName: "pullStatusLabel"
                    Layout.fillWidth: true
                    text: {
                        if (pullCard.failed) {
                            return pullCard.detailText.length > 0
                                ? i18n("Pull failed: %1", pullCard.detailText)
                                : i18n("Pull failed.");
                        }
                        if (pullCard.succeeded) {
                            return i18n("Image pulled.");
                        }
                        if (pullCard.cancelled) {
                            return i18n("Image pull cancelled.");
                        }
                        const parts = [];
                        if (pullCard.statusText.length > 0) {
                            parts.push(pullCard.statusText);
                        }
                        if (pullCard.totalLayers > 0) {
                            parts.push(i18n("%1 of %2 layers", pullCard.completedLayers, pullCard.totalLayers));
                        }
                        return parts.join(" · ");
                    }
                    color: pullCard.failed ? Local.StatusPalette.color("negative") : Kirigami.Theme.textColor
                    opacity: pullCard.active ? 0.85 : 1.0
                    wrapMode: Text.WordWrap
                    elide: Text.ElideRight
                    font: Kirigami.Theme.smallFont
                }
            }

            Accessible.name: pullCard.failed ? i18n("Pull failed: %1", pullCard.detailText) : pullCard.reference
        }
    }
}
