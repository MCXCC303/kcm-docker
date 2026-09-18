/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Docker 相关服务的状态与管理（ARCH_V5_V8 §B1）。

    用户实测：`docker.service` 停掉后本工具仍显示"已连接"（`docker.socket` 还在），
    而且停掉服务后没有任何入口能把它拉起来——因此这里给出三件套（socket / service /
    containerd）的状态，并提供**受限提权**的五个动作（启动 / 停止 / 重启 / 启用 / 禁用）。

    设计要点：
      - 动作走 `controller.controlService(unit, verb)`：白名单校验在 C++（非法请求不发提权动作）
      - 停止 / 禁用要**二次确认**并说明影响（"停掉 socket 后本工具将无法连接"）
      - 状态是只读查询（systemd D-Bus），操作成功后由控制器重新查询
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

import "." as Local

Kirigami.AbstractCard {
    id: root

    /*! 引擎页的 daemon 配置控制器（同时承载服务控制的结果通道）。 */
    required property var controller
    /*! 服务状态来源（`kcm.controller.services`）。 */
    required property var services

    objectName: "serviceCard"
    showClickFeedback: false

    /*! 正在等待确认的 (unit, verb)；确认后才真的发请求。 */
    property string pendingUnit: ""
    property string pendingVerb: ""

    /*! 单位名 → 界面文案（unit 名本身是数据，不翻译）。 */
    function unitLabel(unit: string): string {
        switch (unit) {
        case "docker.socket":
            return i18n("Docker socket");
        case "docker.service":
            return i18n("Docker service");
        case "containerd.service":
            return i18n("containerd service");
        default:
            return unit;
        }
    }

    /*! 状态 key → 文案。 */
    function stateText(stateKey: string): string {
        switch (stateKey) {
        case "running":
            return i18n("Running");
        case "stopped":
            return i18n("Stopped");
        case "failed":
            return i18n("Failed");
        case "starting":
            return i18n("Starting…");
        case "stopping":
            return i18n("Stopping…");
        default:
            return i18n("Unknown");
        }
    }

    /*! 状态 key → 配色（与项目其它状态色一致）。 */
    function stateColor(stateKey: string): color {
        switch (stateKey) {
        case "running":
            return Local.StatusPalette.color("positive");
        case "failed":
            return Local.StatusPalette.color("negative");
        case "stopped":
            return Local.StatusPalette.color("warning");
        default:
            // 未知：不猜状态，用中性色（状态色统一在 StatusPalette 里映射）
            return Local.StatusPalette.color("disabled");
        }
    }

    /*! 动作 key → 文案。 */
    function verbText(verb: string): string {
        switch (verb) {
        case "start":
            return i18n("Start");
        case "stop":
            return i18n("Stop");
        case "restart":
            return i18n("Restart");
        case "enable":
            return i18n("Enable at boot");
        case "disable":
            return i18n("Disable at boot");
        default:
            return verb;
        }
    }

    /*! 危险动作（停止 / 禁用）要二次确认。 */
    function verbNeedsConfirmation(verb: string): bool {
        return verb === "stop" || verb === "disable";
    }

    /*! 请求一个动作：危险的先弹确认，其余直接发。 */
    function requestAction(unit: string, verb: string): void {
        if (root.verbNeedsConfirmation(verb)) {
            root.pendingUnit = unit;
            root.pendingVerb = verb;
            confirmDialog.open();
            return;
        }
        root.controller.controlService(unit, verb);
    }

    /*! 危险动作的后果说明（每种动作说清楚会发生什么）。 */
    function consequenceText(unit: string, verb: string): string {
        if (verb === "stop" && unit === "docker.socket") {
            return i18n("Kontainer will not be able to reach the Docker daemon until the socket is started again.");
        }
        if (verb === "stop") {
            return i18n("Running containers of this daemon will be stopped.");
        }
        if (verb === "disable") {
            return i18n("The service will not be started automatically at boot.");
        }
        return "";
    }

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        Kirigami.Heading {
            Layout.fillWidth: true
            level: 3
            text: i18n("Services")
        }

        QQC2.Label {
            Layout.fillWidth: true
            text: i18n("Managing these units needs administrator rights; Kontainer asks for them when you use a button.")
            font: Kirigami.Theme.smallFont
            opacity: 0.75
            wrapMode: Text.WordWrap
        }

        Kirigami.InlineMessage {
            objectName: "serviceResultMessage"
            Layout.fillWidth: true
            visible: root.controller.serviceErrorKey.length > 0
            type: Kirigami.MessageType.Error
            text: i18n("The service action failed: %1", root.controller.serviceErrorKey)
        }

        Repeater {
            model: root.services.services

            delegate: RowLayout {
                id: serviceRow

                required property var modelData

                objectName: "serviceRow"
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                QQC2.Label {
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 9
                    text: root.unitLabel(serviceRow.modelData.unit)
                    elide: Text.ElideRight
                }

                // 状态 = 颜色点 + 文本（颜色不能是唯一区分手段：文本同样给出来）
                RowLayout {
                    objectName: "serviceStateChip"
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 6
                    spacing: Kirigami.Units.smallSpacing / 2

                    Rectangle {
                        objectName: "serviceStateDot"
                        implicitWidth: Kirigami.Units.smallSpacing * 1.5
                        implicitHeight: implicitWidth
                        radius: width / 2
                        color: root.stateColor(serviceRow.modelData.stateKey)
                        Accessible.ignored: true
                    }
                    QQC2.Label {
                        objectName: "serviceStateLabel"
                        text: root.stateText(serviceRow.modelData.stateKey)
                        elide: Text.ElideRight
                    }
                }

                QQC2.Label {
                    objectName: "serviceEnabledLabel"
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 10
                    visible: serviceRow.modelData.known
                    text: serviceRow.modelData.enabled ? i18n("Enabled at boot") : i18n("Not enabled at boot")
                    font: Kirigami.Theme.smallFont
                    opacity: 0.75
                    elide: Text.ElideRight
                }

                Item {
                    Layout.fillWidth: true
                }

                // 启动 / 停止：按当前状态显示（运行中给"停止"，否则给"启动"）
                QQC2.Button {
                    objectName: "serviceStartButton"
                    visible: serviceRow.modelData.known && !serviceRow.modelData.active
                    text: root.verbText("start")
                    icon.name: "media-playback-start"
                    enabled: !root.controller.serviceInFlight
                    onClicked: root.requestAction(serviceRow.modelData.unit, "start")
                }
                QQC2.Button {
                    objectName: "serviceStopButton"
                    visible: serviceRow.modelData.known && serviceRow.modelData.active
                    text: root.verbText("stop")
                    icon.name: "media-playback-stop"
                    enabled: !root.controller.serviceInFlight
                    onClicked: root.requestAction(serviceRow.modelData.unit, "stop")
                }
                QQC2.Button {
                    objectName: "serviceRestartButton"
                    visible: serviceRow.modelData.known && serviceRow.modelData.active
                    text: root.verbText("restart")
                    icon.name: "view-refresh"
                    enabled: !root.controller.serviceInFlight
                    onClicked: root.requestAction(serviceRow.modelData.unit, "restart")
                }
                QQC2.Button {
                    objectName: "serviceEnableButton"
                    visible: serviceRow.modelData.known && !serviceRow.modelData.enabled
                    text: root.verbText("enable")
                    icon.name: "system-run"
                    enabled: !root.controller.serviceInFlight
                    onClicked: root.requestAction(serviceRow.modelData.unit, "enable")
                }
                QQC2.Button {
                    objectName: "serviceDisableButton"
                    visible: serviceRow.modelData.known && serviceRow.modelData.enabled
                    text: root.verbText("disable")
                    icon.name: "dialog-cancel"
                    enabled: !root.controller.serviceInFlight
                    onClicked: root.requestAction(serviceRow.modelData.unit, "disable")
                }

                QQC2.BusyIndicator {
                    objectName: "serviceBusyIndicator"
                    visible: root.controller.serviceInFlight
                    running: root.controller.serviceInFlight
                    implicitWidth: Kirigami.Units.iconSizes.smallMedium
                    implicitHeight: Kirigami.Units.iconSizes.smallMedium
                }
            }
        }

        QQC2.Label {
            objectName: "serviceUnavailableHint"
            Layout.fillWidth: true
            visible: {
                const list = root.services.services;
                return list.length === 0 || !list[0].known;
            }
            text: i18n("The state of these services cannot be read (systemd is not reachable).")
            font: Kirigami.Theme.smallFont
            opacity: 0.75
            wrapMode: Text.WordWrap
        }
    }

    Local.ConfirmDialog {
        id: confirmDialog

        objectName: "serviceConfirmDialog"
        headingText: root.verbText(root.pendingVerb)
        questionText: i18n("%1 “%2”?", root.verbText(root.pendingVerb), root.unitLabel(root.pendingUnit))
        consequenceText: root.consequenceText(root.pendingUnit, root.pendingVerb)
        acceptText: root.verbText(root.pendingVerb)
        destructive: true
        onConfirmed: root.controller.controlService(root.pendingUnit, root.pendingVerb)
    }
}
