/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    键值对编辑器（ARCH_V5_V8 §1.6）。

    环境变量、容器/网络/卷的标签、Dockerfile 构建参数都用它，避免每处再写一套
    "一行两个输入框 + 增删"。

    三件必须由它统一承担的事：

      - **键名校验只有一份实现**：调用 C++ 的 `Presentation.isValidEnvKey()`，
        QML 里不写第二份正则
      - **值可以当密码显示**（`secretValues`）：环境变量常含密钥，默认不回显
      - **粘贴 `.env` 文本**：解析也走 C++（`Presentation.parseEnvText()`），
        支持注释、空行、`export ` 前缀与引号

    用法：

        Components.KeyValueListEditor {
            id: envEditor
            initialEntries: [{ key: "TZ", value: "Asia/Shanghai" }]
            keyPlaceholderText: "TZ"
            secretValues: true
            onChanged: root.markDirty()
        }
*/

// delegate 需要访问外层 id（调用 ListModel 的增删改）：固定用 Unbound 语义
pragma ComponentBehavior: Unbound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.kontainer as Kontainer

import "." as Local

ColumnLayout {
    id: root

    /*! 初始条目：`[{ key, value }]`（组件创建时读取一次）。 */
    property var initialEntries: []
    /*! 键的占位文本（例如 `TZ`）。 */
    property string keyPlaceholderText: "KEY"
    /*! 值的占位文本。 */
    property string valuePlaceholderText: "value"
    /*! 值默认以密码样式显示（环境变量等可能是密钥）。 */
    property bool secretValues: false
    /*! 是否显示"粘贴 .env"按钮。 */
    property bool envPasteEnabled: false

    /*! 条目发生变化。 */
    signal changed

    spacing: Kirigami.Units.smallSpacing

    ListModel {
        id: rows

        Component.onCompleted: {
            for (const entry of root.initialEntries) {
                rows.append({
                    entryKey: String(entry.key ?? ""),
                    entryValue: String(entry.value ?? "")
                });
            }
        }
    }

    Repeater {
        model: rows

        delegate: ColumnLayout {
            required property int index
            required property string entryKey
            required property string entryValue

            objectName: "keyValueRow"
            Layout.fillWidth: true
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                QQC2.TextField {
                    id: keyField

                    objectName: "keyValueKeyField"
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 10
                    text: entryKey
                    placeholderText: root.keyPlaceholderText
                    onTextEdited: {
                        root.changed();
                        rows.setProperty(index, "entryKey", text);
                    }
                }

                QQC2.TextField {
                    id: valueField

                    objectName: "keyValueValueField"
                    Layout.fillWidth: true
                    text: entryValue
                    placeholderText: root.valuePlaceholderText
                    echoMode: revealSecret.checked || !root.secretValues ? TextInput.Normal : TextInput.Password
                    onTextEdited: {
                        root.changed();
                        rows.setProperty(index, "entryValue", text);
                    }
                }

                // 只有"值可能是密钥"的场景才需要显隐切换
                QQC2.ToolButton {
                    objectName: "keyValueRevealButton"
                    visible: root.secretValues
                    checkable: true
                    id: revealSecret
                    icon.name: checked ? "password-show-off" : "password-show-on"
                    Accessible.name: i18n("Show value")
                    QQC2.ToolTip.text: i18n("Show value")
                    QQC2.ToolTip.visible: hovered
                }

                QQC2.ToolButton {
                    objectName: "keyValueRemoveButton"
                    icon.name: "list-remove"
                    Accessible.name: i18n("Remove")
                    onClicked: {
                        rows.remove(index);
                        root.changed();
                    }
                }
            }

            QQC2.Label {
                objectName: "keyValueKeyError"
                Layout.fillWidth: true
                visible: text.length > 0
                text: keyField.text.length === 0 || Kontainer.Presentation.isValidEnvKey(keyField.text)
                    ? ""
                    : i18n("Invalid name: use letters, digits and underscore, and do not start with a digit.")
                color: Local.StatusPalette.color("negative")
                wrapMode: Text.WordWrap
                font: Kirigami.Theme.smallFont
            }

            QQC2.Label {
                objectName: "keyValueDuplicateError"
                Layout.fillWidth: true
                visible: text.length > 0
                text: {
                    if (keyField.text.length === 0) {
                        return "";
                    }
                    for (let i = 0; i < rows.count; ++i) {
                        if (i !== index && String(rows.get(i).entryKey) === keyField.text) {
                            return i18n("Duplicate name.");
                        }
                    }
                    return "";
                }
                color: Local.StatusPalette.color("negative")
                wrapMode: Text.WordWrap
                font: Kirigami.Theme.smallFont
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.Button {
            objectName: "keyValueAddButton"
            text: i18n("Add")
            icon.name: "list-add"
            onClicked: {
                rows.append({
                    entryKey: "",
                    entryValue: ""
                });
                root.changed();
            }
        }

        QQC2.Button {
            objectName: "keyValuePasteEnvButton"
            visible: root.envPasteEnabled
            text: i18n("Paste .env…")
            icon.name: "edit-paste"
            onClicked: envPasteDialog.open()
        }

        Item {
            Layout.fillWidth: true
        }
    }

    /*! 当前条目（丢掉完全为空的行）。 */
    function entries(): var {
        const result = [];
        for (let i = 0; i < rows.count; ++i) {
            const row = rows.get(i);
            const key = String(row.entryKey).trim();
            if (key.length === 0) {
                continue;
            }
            result.push({
                key: key,
                value: String(row.entryValue)
            });
        }
        return result;
    }

    /*! 是否有键名非法或重复（空行不算错误，提交时会被忽略）。 */
    function hasErrors(): bool {
        const seen = [];
        for (let i = 0; i < rows.count; ++i) {
            const key = String(rows.get(i).entryKey).trim();
            if (key.length === 0) {
                continue;
            }
            if (!Kontainer.Presentation.isValidEnvKey(key)) {
                return true;
            }
            if (seen.indexOf(key) >= 0) {
                return true;
            }
            seen.push(key);
        }
        return false;
    }

    /*! 用新的一组条目替换现有内容。 */
    function setEntries(list): void {
        rows.clear();
        for (const entry of list) {
            rows.append({
                entryKey: String(entry.key ?? ""),
                entryValue: String(entry.value ?? "")
            });
        }
        root.changed();
    }

    /*! 追加一组条目（粘贴 .env 用），已存在的键会被覆盖。 */
    function appendEntries(list): void {
        for (const entry of list) {
            let replaced = false;
            for (let i = 0; i < rows.count; ++i) {
                if (String(rows.get(i).entryKey).trim() === entry.key) {
                    rows.setProperty(i, "entryValue", entry.value);
                    replaced = true;
                    break;
                }
            }
            if (!replaced) {
                rows.append({
                    entryKey: entry.key,
                    entryValue: entry.value
                });
            }
        }
        root.changed();
    }

    /*! 粘贴 `.env` 文本的输入框（多行，确认后合并）。 */
    Kirigami.PromptDialog {
        id: envPasteDialog

        objectName: "envPasteDialog"
        title: i18n("Paste .env content")
        dialogType: Kirigami.PromptDialog.Information
        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
        onAccepted: root.appendEntries(Kontainer.Presentation.parseEnvText(envPasteField.text))

        QQC2.ScrollView {
            Layout.preferredWidth: Kirigami.Units.gridUnit * 24
            Layout.preferredHeight: Kirigami.Units.gridUnit * 10

            QQC2.TextArea {
                id: envPasteField

                objectName: "envPasteField"
                // 示例文本是数据而不是界面文案
                placeholderText: "TZ=Asia/Shanghai\n# comment\nAPI_KEY=\"secret\"" // i18n-lint: allow 示例 .env 内容
                font.family: "monospace"
                wrapMode: TextEdit.NoWrap
            }
        }
    }
}
