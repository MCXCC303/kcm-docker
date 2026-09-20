/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Container log console (ARCH_V5_V8 §3.1.3).

    Deliberately a **single read-only monospace TextArea**, not a "ListView with one
    delegate per line":

      - logs append at high frequency, and per-line delegates keep creating/destroying
        items and triggering relayout — the shape that induced the phase-3 segfault
        (ARCH_V3 appendix A.1d/A.1g);
      - console semantics are "one block of text + scrolling", and select-to-copy then
        behaves most conventionally.

    The price is double caps (enforced by `ContainerLogController`; this file only shows
    "N lines omitted").
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

import "." as Local
import ".." as Components

ColumnLayout {
    // Do not name the id `console`: it shadows the JS global console (QML errors out immediately)
    id: logConsole

    /*! `ContainerLogController` (the logs controller of the container detail page). */
    required property var logs

    objectName: "logConsole"
    spacing: Kirigami.Units.smallSpacing

    /*! Whether to follow the tail (scrolling up or pausing manually should stop it). */
    property bool followTail: true

    readonly property bool empty: logConsole.logs.lineCount === 0
    readonly property bool paused: logConsole.logs.paused

    /*! State key → text (C++ supplies only the key). */
    function stateText(): string {
        switch (logConsole.logs.stateKey) {
        case "connecting":
            return i18n("Connecting…");
        case "streaming":
            return i18n("Following");
        case "paused":
            return i18n("Paused (output is buffered)");
        case "ended":
            return i18n("The log stream ended. The container may have stopped.");
        case "failed":
            return logConsole.failureText();
        default:
            return i18n("Not connected.");
        }
    }

    /*! Failure key → text (e.g. "driver cannot be read", which must offer an alternative). */
    function failureText(): string {
        switch (logConsole.logs.errorKey) {
        case "containerGone":
            return i18n("The container no longer exists.");
        case "driverUnsupported":
            return i18n("This container's logging driver does not support reading logs from the engine. Use the docker CLI or the container's own log destination.");
        case "dockerUnavailable":
            return i18n("Docker Engine is not reachable.");
        case "timeout":
            return i18n("Reading logs timed out.");
        case "permissionDenied":
            return i18n("Reading this container's logs was denied.");
        default:
            return i18n("Reading logs failed.");
        }
    }

    function scrollToEnd(): void {
        const area = logArea;
        area.cursorPosition = area.length;
        logScroll.contentItem.contentY = Math.max(0, logScroll.contentHeight - logScroll.height);
    }

    Connections {
        target: logConsole.logs

        // Scroll along with new content (pausing appends nothing, so nothing scrolls)
        function onAppended() {
            if (logConsole.followTail) {
                Qt.callLater(logConsole.scrollToEnd);
            }
        }
    }

    /* ---------------- Action bar ---------------- */
    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            objectName: "logStateLabel"
            Layout.fillWidth: true
            text: logConsole.stateText()
            elide: Text.ElideRight
            opacity: 0.8
        }

        QQC2.Label {
            objectName: "logTruncatedLabel"
            visible: logConsole.logs.droppedLineCount > 0
            text: i18np("Earlier output omitted (%1 line)", "Earlier output omitted (%1 lines)", logConsole.logs.droppedLineCount)
            font: Kirigami.Theme.smallFont
            opacity: 0.7
        }

        QQC2.CheckBox {
            objectName: "logFollowCheck"
            checked: logConsole.followTail
            text: i18n("Follow")
            onToggled: logConsole.followTail = checked
        }

        QQC2.Button {
            objectName: "logPauseButton"
            visible: logConsole.logs.stateKey === "streaming" || logConsole.paused
            text: logConsole.paused ? i18n("Resume") : i18n("Pause")
            icon.name: logConsole.paused ? "media-playback-start" : "media-playback-pause"
            onClicked: {
                if (logConsole.paused) {
                    logConsole.logs.resume();
                    logConsole.followTail = true;
                } else {
                    logConsole.logs.pause();
                }
            }
        }

        QQC2.Button {
            objectName: "logClearButton"
            text: i18n("Clear")
            icon.name: "edit-clear"
            enabled: !logConsole.empty
            onClicked: logConsole.logs.clear()
        }

        Local.CopyButton {
            objectName: "logCopyAllButton"
            value: logConsole.logs.text
            fieldLabel: i18n("log output")
            visible: !logConsole.empty
        }

        QQC2.Button {
            objectName: "logReconnectButton"
            visible: logConsole.logs.stateKey === "ended" || logConsole.logs.stateKey === "failed"
            text: i18n("Reconnect")
            icon.name: "view-refresh"
            onClicked: {
                logConsole.followTail = true;
                logConsole.logs.reconnect();
            }
        }
    }

    /* ---------------- Failure message (with an alternative) ---------------- */
    Kirigami.InlineMessage {
        objectName: "logErrorMessage"
        Layout.fillWidth: true
        visible: logConsole.logs.stateKey === "failed"
        type: Kirigami.MessageType.Error
        text: logConsole.logs.errorText.length > 0
            ? logConsole.failureText() + " " + i18n("Details: %1", logConsole.logs.errorText)
            : logConsole.failureText()
    }

    /* ---------------- The console itself ---------------- */
    Kirigami.Separator {
        Layout.fillWidth: true
    }

    QQC2.ScrollView {
        id: logScroll

        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        // No wrapping: console semantics (long lines scroll sideways) and wrapping grows the line count.
        // Do **not** bind contentWidth to availableWidth: the Vertical/HorizontalScrollBar
        // visibility in turn affects availableWidth, forming a loop (caught by tst_kcm_widget_churn)

        QQC2.TextArea {
            id: logArea

            objectName: "logTextArea"
            // The controller trims the text to the caps; this only displays it
            text: logConsole.logs.text
            readOnly: true
            selectByMouse: true
            wrapMode: TextEdit.NoWrap
            font.family: "monospace"
            font.pointSize: Kirigami.Theme.smallFont.pointSize
            // Read-only text should not steal focus but must stay selectable for copying
            activeFocusOnPress: false

            Accessible.name: i18n("Container log output")
            Accessible.role: Accessible.StaticText
        }
    }

    EmptyPlaceholder {
        objectName: "logEmptyPlaceholder"
        Layout.fillWidth: true
        Layout.fillHeight: true
        visible: logConsole.empty
        iconName: "utilities-terminal"
        message: logConsole.logs.stateKey === "failed" ? "" : i18n("No log output yet.")
        explanationText: logConsole.logs.stateKey === "connecting"
            ? i18n("Waiting for the first log lines…")
            : i18n("This container has not written anything to stdout or stderr yet.")
    }

    // The placeholder above must also state "not connected yet"; this adds a hint while it is hidden
    QQC2.Label {
        objectName: "logIdleHint"
        Layout.fillWidth: true
        visible: logConsole.logs.stateKey === "idle"
        text: i18n("Logs are read only while this tab is open.")
        font: Kirigami.Theme.smallFont
        opacity: 0.7
    }
}
