/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    字符串列表编辑器（ARCH_V5_V8 §1.6 的补充）。

    用于镜像加速器、insecure-registries 这类"一串字符串"的字段：
    增、删、上移下移（顺序对镜像源有意义：靠前的先被尝试）。

    校验通过 `validator` 回调注入（返回空字符串表示合法，否则返回错误文案），
    这样"什么算合法的镜像源地址"只有一份实现。

    用法：

        Components.StringListEditor {
            id: mirrors
            initialEntries: ["https://mirror.example.com"]
            validator: function (value) { return root.validateMirror(value); }
            onChanged: root.markDirty()
        }
*/

// delegate 需要访问外层 id（调用 ListModel 的增删改）：固定用 Unbound 语义
pragma ComponentBehavior: Unbound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

import "." as Local

ColumnLayout {
    id: root

    /*! 初始条目（组件创建时读取一次）。 */
    property var initialEntries: []
    /*!
     * 校验回调：`function(value) -> string`。
     * 返回空字符串表示合法；返回文案会在该行下方显示，并阻止提交。
     */
    property var validator: null
    /*! 添加按钮文案。 */
    property string addText: i18n("Add")
    /*! 输入占位文本。 */
    property string placeholderText: ""

    /*! 条目发生变化（增删改序）。 */
    signal changed

    /*! 是否可编辑；受保护区未解锁时为 false（字段看得见但改不了）。 */
    property bool editable: true

    spacing: Kirigami.Units.smallSpacing

    ListModel {
        id: entries

        Component.onCompleted: root.syncFromInitialEntries()
    }

    /*!
     * 把控制器给出的列表同步进模型。
     *
     * 为什么要判断"内容是否相同"：自动刷新会让 `initialEntries` 重新求值。
     * 无条件重建会把用户正在输入的那一行（以及光标位置）一起丢掉；
     * 而内容相同时重建没有任何意义——用户在控件里的改动本来就会立刻写回控制器，
     * 因此两边一致恰恰是"不需要动"的信号。真正的外部变化（重新读盘、恢复备份）
     * 才会走到重建这一步。
     */
    onInitialEntriesChanged: root.syncFromInitialEntries()

    function syncFromInitialEntries(): void {
        const incoming = [];
        for (const value of root.initialEntries) {
            incoming.push(String(value));
        }
        // 与"当前条目的**有意义**内容"比较，而不是逐行比较原始值：
        // 用户刚点「添加」得到的是一个空行（待填写的草稿），而 values() 会把空行丢掉，
        // 于是控制器里的列表并不包含它。逐行比较会把这个空行当成"外部变化"清掉——
        // 表现为"点了添加，只是变成未保存，条目没出现"（真实反馈）。
        const current = root.values();
        if (incoming.length === current.length) {
            let identical = true;
            for (let i = 0; i < incoming.length; ++i) {
                if (current[i] !== incoming[i].trim()) {
                    identical = false;
                    break;
                }
            }
            if (identical) {
                return;
            }
        }
        entries.clear();
        for (const value of incoming) {
            entries.append({
                value: value
            });
        }
    }

    Repeater {
        model: entries

        delegate: RowLayout {
            required property int index
            required property string value

            objectName: "stringEntryRow"
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                QQC2.TextField {
                    id: valueField

                    objectName: "stringEntryField"
                    Layout.fillWidth: true
                    text: parent.parent.value
                    readOnly: !root.editable
                    placeholderText: root.placeholderText
                    onTextEdited: {
                        root.changed();
                        entries.setProperty(index, "value", text);
                    }
                }

                QQC2.Label {
                    objectName: "stringEntryError"
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: root.validator ? root.validator(valueField.text) : ""
                    color: Local.StatusPalette.color("negative")
                    wrapMode: Text.WordWrap
                    font: Kirigami.Theme.smallFont
                }
            }

            QQC2.ToolButton {
                objectName: "stringEntryUpButton"
                icon.name: "go-up"
                enabled: root.editable && index > 0
                Accessible.name: i18n("Move up")
                onClicked: {
                    // 顺序很重要：先发信号，再改模型 —— remove()/move() 会同步销毁当前 delegate，
                    // 之后再访问外层 id 会抛 ReferenceError（真实踩过）
                    root.changed();
                    entries.move(index, index - 1, 1);
                }
            }

            QQC2.ToolButton {
                objectName: "stringEntryDownButton"
                icon.name: "go-down"
                enabled: root.editable && index < entries.count - 1
                Accessible.name: i18n("Move down")
                onClicked: {
                    root.changed();
                    entries.move(index, index + 1, 1);
                }
            }

            QQC2.ToolButton {
                objectName: "stringEntryRemoveButton"
                icon.name: "list-remove"
                enabled: root.editable
                Accessible.name: i18n("Remove")
                onClicked: {
                    root.changed();
                    entries.remove(index);
                }
            }
        }
    }

    QQC2.Button {
        objectName: "stringListAddButton"
        Layout.alignment: Qt.AlignLeft
        text: root.addText
        icon.name: "list-add"
        enabled: root.editable
        onClicked: {
            entries.append({
                value: ""
            });
            root.changed();
        }
    }

    /*! 当前条目（去掉首尾空白，丢掉空行）。 */
    function values(): var {
        const result = [];
        for (let i = 0; i < entries.count; ++i) {
            const value = String(entries.get(i).value).trim();
            if (value.length > 0) {
                result.push(value);
            }
        }
        return result;
    }

    /*! 是否有条目没通过校验（空行不算错误，提交时会被忽略）。 */
    function hasErrors(): bool {
        if (!root.validator) {
            return false;
        }
        for (let i = 0; i < entries.count; ++i) {
            const value = String(entries.get(i).value);
            if (value.trim().length === 0) {
                continue;
            }
            if (root.validator(value).length > 0) {
                return true;
            }
        }
        return false;
    }

    /*! 用新的一组值替换现有内容。 */
    function setValues(list): void {
        entries.clear();
        for (const value of list) {
            entries.append({
                value: String(value)
            });
        }
        root.changed();
    }
}
