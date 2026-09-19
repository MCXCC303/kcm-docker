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
    /*
     * 控制器 → 编辑缓冲。
     *
     * **就地更新**，行数不变时绝不 clear()+append()：重建 ListModel 会销毁正在输入的
     * delegate（焦点与光标一起丢），这本身也会造成"输入被打断"。
     * 行数变化（增删行）时才重建，这是必要的。
     */
    function syncRows(): void {
        const rows = editor.controller.portRows;
        if (portRows.count === rows.length) {
            for (let i = 0; i < rows.length; ++i) {
                const row = rows[i];
                const item = portRows.get(i);
                if (item.containerPort !== (row.containerPort ?? 0)) {
                    portRows.setProperty(i, "containerPort", row.containerPort ?? 0);
                }
                if (item.hostPort !== (row.hostPort ?? 0)) {
                    portRows.setProperty(i, "hostPort", row.hostPort ?? 0);
                }
                if (item.hostIp !== (row.hostIp ?? "")) {
                    portRows.setProperty(i, "hostIp", row.hostIp ?? "");
                }
                if (item.protocol !== (row.protocol ?? "tcp")) {
                    portRows.setProperty(i, "protocol", row.protocol ?? "tcp");
                }
            }
            return;
        }
        portRows.clear();
        for (const row of rows) {
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

            /*
             * 宿主端口用**带校验的文本框**而不是 SpinBox。
             *
             * 用户实测：SpinBox 在每次外部改值时会重排自己的文本（并把光标推到末尾），
             * 于是"输入 8000"变成 8→重新选中→0→重新选中…… 这里改用 TextField：
             *  - 输入期间**不回写**控制器（只在 editingFinished 时回写），因此不会有
             *    "模型 → 文本"的回环把光标弄丢；
             *  - 校验交给 IntValidator（0…65535；留空/0 = 随机分配，由下面的提示说明）。
             */
            QQC2.TextField {
                objectName: "wizardHostPort"
                Layout.preferredWidth: Kirigami.Units.gridUnit * 6
                placeholderText: i18n("random")
                Accessible.name: i18n("Host port")
                horizontalAlignment: TextInput.AlignRight
                inputMethodHints: Qt.ImhDigitsOnly
                text: portRowItem.hostPort === 0 ? "" : String(portRowItem.hostPort)
                validator: IntValidator {
                    bottom: 0
                    top: 65535
                }
                // 只在输入结束时回写：输入过程中不动模型，光标因此不会被抢走
                onEditingFinished: editor.setField(portRowItem.index, "hostPort", text.length === 0 ? 0 : parseInt(text, 10))
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
            QQC2.TextField {
                objectName: "wizardContainerPort"
                Layout.preferredWidth: Kirigami.Units.gridUnit * 6
                Accessible.name: i18n("Container port")
                horizontalAlignment: TextInput.AlignRight
                inputMethodHints: Qt.ImhDigitsOnly
                text: portRowItem.containerPort === 0 ? "" : String(portRowItem.containerPort)
                validator: IntValidator {
                    bottom: 1
                    top: 65535
                }
                // 同上：失焦/回车才回写，输入中间不打断
                onEditingFinished: editor.setField(portRowItem.index,
                                                   "containerPort",
                                                   text.length === 0 ? 0 : parseInt(text, 10))
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
