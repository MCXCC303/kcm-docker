/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    容器日志控制台（ARCH_V5_V8 §3.1.3）。

    刻意用**单块只读等宽 TextArea**，不是"每行一个 delegate 的 ListView"：

      - 日志是高频追加场景，逐行 delegate 会不断创建/销毁条目并触发布局重排 ——
        这正是三期段错误（ARCH_V3 附录 A.1d/A.1g）的诱因形状；
      - 控制台语义本来就是"一整块文本 + 滚动"，选择复制的行为也最标准。

    代价是必须有双上限（由 `ContainerLogController` 守住，这里只显示"已省略 N 行"）。
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

import "." as Local
import ".." as Components

ColumnLayout {
    // 注意不要把 id 取成 `console`：那会遮蔽 JS 的全局 console（QML 直接报错）
    id: logConsole

    /*! `ContainerLogController`（容器详情的 logs 控制器）。 */
    required property var logs

    objectName: "logConsole"
    spacing: Kirigami.Units.smallSpacing

    /*! 是否跟随末尾（用户往上翻或手动暂停时应当停止跟随）。 */
    property bool followTail: true

    readonly property bool empty: logConsole.logs.lineCount === 0
    readonly property bool paused: logConsole.logs.paused

    /*! 状态 key → 文案（C++ 只给 key）。 */
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

    /*! 失败原因 key → 文案（含"日志驱动不支持读取"这种必须给替代做法的情形）。 */
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

        // 有新内容就跟着滚（暂停时不追加、也就不滚动）
        function onAppended() {
            if (logConsole.followTail) {
                Qt.callLater(logConsole.scrollToEnd);
            }
        }
    }

    /* ---------------- 动作条 ---------------- */
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

    /* ---------------- 失败提示（含替代做法） ---------------- */
    Kirigami.InlineMessage {
        objectName: "logErrorMessage"
        Layout.fillWidth: true
        visible: logConsole.logs.stateKey === "failed"
        type: Kirigami.MessageType.Error
        text: logConsole.logs.errorText.length > 0
            ? logConsole.failureText() + " " + i18n("Details: %1", logConsole.logs.errorText)
            : logConsole.failureText()
    }

    /* ---------------- 控制台本体 ---------------- */
    Kirigami.Separator {
        Layout.fillWidth: true
    }

    QQC2.ScrollView {
        id: logScroll

        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        // 不换行：控制台语义（一行长文本靠横向滚动看），也避免换行使行数失控。
        // 注意**不要**把 contentWidth 绑到 availableWidth：Vertical/HorizontalScrollBar 的可见性
        // 又反过来影响 availableWidth，会形成绑定环（tst_kcm_widget_churn 抓到了这个）

        QQC2.TextArea {
            id: logArea

            objectName: "logTextArea"
            // 文本由控制器按上限裁剪，这里只负责显示
            text: logConsole.logs.text
            readOnly: true
            selectByMouse: true
            wrapMode: TextEdit.NoWrap
            font.family: "monospace"
            font.pointSize: Kirigami.Theme.smallFont.pointSize
            // 只读文本不该抢焦点，但必须可以被选中复制
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

    // 上面那个占位在"还没连接"时也要说明状态；这里补一条不可见时的提示
    QQC2.Label {
        objectName: "logIdleHint"
        Layout.fillWidth: true
        visible: logConsole.logs.stateKey === "idle"
        text: i18n("Logs are read only while this tab is open.")
        font: Kirigami.Theme.smallFont
        opacity: 0.7
    }
}
