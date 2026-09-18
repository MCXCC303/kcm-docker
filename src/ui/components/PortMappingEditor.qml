/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    端口映射编辑器（节点图风格）。

    创建容器时要填端口，用户实测反馈"建议同样使用节点图风格，并且标注宿主机/容器"：
    端口拓扑（PortTopology）是**只读展示**，这里要的是**可编辑**的同一个视觉语言——
    左边宿主机（IP + 端口），中间一条带圆点的连线，右边容器端口，顶部标注两侧是什么。

    为什么行编辑走控制器：delegate 在 `pragma ComponentBehavior: Unbound` 下拿不到根对象 id
    （见 CreateContainer.qml 里的说明），因此这里只通过 `editor` 这个**非根**中转对象回写。

    用法：

        Components.PortMappingEditor {
            controller: page.controller
            Layout.fillWidth: true
        }
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

import "." as Local

ColumnLayout {
    id: root

    /*! 创建向导的控制器（提供 addPortRow/setPortRow/removePortRow）。 */
    required property var controller

    objectName: "portMappingEditor"
    spacing: Kirigami.Units.smallSpacing

    /*!
     * delegate 用的中转对象：Unbound 下 delegate 里不能引用根对象 id。
     */
    QtObject {
        id: editor

        readonly property var controller: root.controller
        readonly property var model: root.portRows

        function add() {
            editor.controller.addPortRow(80, 0, "", "tcp");
        }
        function setField(row, field, value) {
            editor.controller.setPortRow(row, field, value);
        }
        function remove(row) {
            editor.controller.removePortRow(row);
        }
    }

    ListModel {
        id: portRows
    }

    /*! 控制器里的行 → 编辑缓冲（内容一致时不重建，避免打断正在输入的行）。 */
    function syncRows(): void {
        if (rowsEqual(portRows, editor.controller.portRows)) {
            return;
        }
        portRows.clear();
        for (const row of editor.controller.portRows) {
            portRows.append({
                containerPort: row.containerPort ?? 0,
                hostPort: row.hostPort ?? 0,
                hostIp: row.hostIp ?? "",
                protocol: row.protocol ?? "tcp"
            });
        }
    }

    function rowsEqual(model, rows): bool {
        if (model.count !== rows.length) {
            return false;
        }
        for (let i = 0; i < model.count; ++i) {
            const item = model.get(i);
            const row = rows[i];
            for (const field of ["containerPort", "hostPort", "hostIp", "protocol"]) {
                if (String(item[field] ?? "") !== String(row[field] ?? "")) {
                    return false;
                }
            }
        }
        return true;
    }

    Connections {
        target: root.controller
        function onChanged() {
            root.syncRows();
        }
    }

    Component.onCompleted: root.syncRows()

    /* ---------------------------- 两侧的标注 ---------------------------- */
    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            objectName: "portEditorHostLabel"
            Layout.preferredWidth: Kirigami.Units.gridUnit * 12
            text: i18n("Host")
            font.bold: true
            opacity: 0.8
        }

        Item {
            Layout.fillWidth: true
        }

        QQC2.Label {
            objectName: "portEditorContainerLabel"
            Layout.preferredWidth: Kirigami.Units.gridUnit * 7
            text: i18n("Container")
            font.bold: true
            opacity: 0.8
            horizontalAlignment: Text.AlignRight
        }
    }

    Repeater {
        model: portRows

        delegate: RowLayout {
            id: portRowItem

            required property int index
            required property int containerPort
            required property int hostPort
            required property string hostIp
            required property string protocol

            objectName: "wizardPortRow"
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            /* ---- 宿主机一侧：IP + 端口（0 = 随机） ---- */
            QQC2.TextField {
                objectName: "wizardPortHostIp"
                Layout.preferredWidth: Kirigami.Units.gridUnit * 5
                placeholderText: "0.0.0.0"
                Accessible.name: i18n("Host address")
                text: portRowItem.hostIp
                onTextEdited: editor.setField(portRowItem.index, "hostIp", text)
            }

            QQC2.SpinBox {
                objectName: "wizardHostPort"
                from: 0
                to: 65535
                value: portRowItem.hostPort
                textFromValue: function (value) {
                    return value === 0 ? i18n("random") : value.toString();
                }
                onValueModified: editor.setField(portRowItem.index, "hostPort", value)
            }

            /* ---- 中间的连线：与端口拓扑同一视觉语言（两端插座圆点） ----
               用户要求"复用容器信息里网络图的节点图样式，颜色可以任意指定，
               编辑时不需要做特征标注"：因此这里只画线 + 两端的插座圆点，没有文字；
               颜色按**行内容**取（同一个映射永远同色，不同映射彼此可区分），
               不表达任何语义（纯装饰，Accessible.ignored）。 */
            Canvas {
                objectName: "wizardPortLink"
                Layout.fillWidth: true
                Layout.minimumWidth: Kirigami.Units.gridUnit * 2
                Layout.preferredHeight: Kirigami.Units.gridUnit
                Accessible.ignored: true

                /*! 该行的连线颜色（可被用例读取，用来断言"每行一种颜色"）。 */
                readonly property color linkColor: Local.ChartPalette.connectionColor(
                    "editor|" + portRowItem.containerPort + "/" + portRowItem.protocol
                    + "|" + portRowItem.hostIp + ":" + portRowItem.hostPort)

                readonly property real lineWidth: Math.max(2, Math.round(Kirigami.Units.gridUnit * 0.28))
                readonly property real dotRadius: lineWidth * 1.15

                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                Component.onCompleted: requestPaint()

                onPaint: {
                    const ctx = getContext("2d");
                    ctx.reset();
                    const middle = height / 2;
                    const left = dotRadius;
                    const right = width - dotRadius;
                    if (right <= left) {
                        return;
                    }

                    ctx.lineWidth = lineWidth;
                    ctx.lineCap = "round";
                    ctx.strokeStyle = linkColor;
                    ctx.beginPath();
                    ctx.moveTo(left, middle);
                    ctx.lineTo(right, middle);
                    ctx.stroke();

                    // 两端都画成"插座"：外圈连线色、中心掏空成背景色（与拓扑一致）
                    for (const x of [left, right]) {
                        ctx.fillStyle = linkColor;
                        ctx.beginPath();
                        ctx.arc(x, middle, dotRadius, 0, Math.PI * 2);
                        ctx.fill();
                        ctx.fillStyle = Kirigami.Theme.backgroundColor;
                        ctx.beginPath();
                        ctx.arc(x, middle, dotRadius * 0.42, 0, Math.PI * 2);
                        ctx.fill();
                    }
                }
            }

            /* ---- 容器一侧：端口 + 协议 ---- */
            QQC2.SpinBox {
                objectName: "wizardContainerPort"
                from: 1
                to: 65535
                value: portRowItem.containerPort
                onValueModified: editor.setField(portRowItem.index, "containerPort", value)
            }

            QQC2.ComboBox {
                objectName: "wizardPortProtocol"
                Layout.preferredWidth: Kirigami.Units.gridUnit * 5
                textRole: "text"
                valueRole: "value"
                model: [
                    {text: i18n("TCP"), value: "tcp"},
                    {text: i18n("UDP"), value: "udp"}
                ]
                Component.onCompleted: currentIndex = indexOfValue(portRowItem.protocol)
                onActivated: editor.setField(portRowItem.index, "protocol", currentValue)
            }

            QQC2.Button {
                objectName: "wizardRemovePort"
                icon.name: "list-remove"
                flat: true
                Accessible.name: i18n("Remove this port")
                onClicked: editor.remove(portRowItem.index)
            }
        }
    }

    QQC2.Label {
        Layout.fillWidth: true
        text: i18n("A host port of 0 (shown as “random”) lets Docker pick a free one.")
        font: Kirigami.Theme.smallFont
        opacity: 0.75
        wrapMode: Text.WordWrap
    }

    QQC2.Button {
        objectName: "wizardAddPort"
        text: i18n("Add port")
        icon.name: "list-add"
        onClicked: editor.add()
    }

    QQC2.Label {
        objectName: "wizardNoPorts"
        Layout.fillWidth: true
        visible: portRows.count === 0
        text: i18n("No published ports. The container will only be reachable on its networks.")
        font: Kirigami.Theme.smallFont
        opacity: 0.75
        wrapMode: Text.WordWrap
    }
}
