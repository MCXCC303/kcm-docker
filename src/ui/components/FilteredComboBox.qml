/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    可搜索的下拉选择（ARCH_V5_V8 §F3）。

    用户实测："镜像选择、挂载预设选择建议做成下拉菜单选择（考虑到部分用户镜像版本众多）"。
    纯 `QQC2.ComboBox` 没有内置搜索，而自定义弹层在离屏测试里不可靠（本项目已经踩过两次），
    因此这里做成**搜索框 + 下拉**的组合：

        - 搜索框在上：输入即过滤（不区分大小写，匹配 `textRole` 字段）
        - 下拉在下：只列出过滤后的条目；选中后 `selected(entry)` 把**整条数据**交回调用方
        - entries 必须是**属性**（不是函数调用）：函数调用不建立依赖，列表不会跟着更新
          （这个坑在日志、网络、数据卷、预设上各踩过一次）

    用法：

        Components.FilteredComboBox {
            entries: page.controller.availableImages
            textRole: "reference"
            onSelected: function (entry) { page.controller.image = entry.reference; }
        }
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

ColumnLayout {
    id: root

    /*! 完整条目列表（`[{...}]`，必须是属性）。 */
    required property var entries
    /*! 用来显示的字段名，也用于搜索匹配。 */
    property string textRole: "text"
    /*! 搜索框的占位文案。 */
    property string searchPlaceholder: i18n("Search…")
    /*! 下拉框没选中时的占位文案。 */
    property string placeholder: i18n("Choose…")

    /*! 选中一条（交回整条数据，调用方自己取需要的字段）。 */
    signal selected(var entry)

    objectName: "filteredComboBox"
    spacing: Kirigami.Units.smallSpacing / 2

    /*! 过滤后的条目（搜索为空时是全部）。 */
    readonly property var filteredEntries: {
        const needle = searchField.text.trim().toLowerCase();
        if (needle.length === 0) {
            return root.entries;
        }
        const result = [];
        for (const entry of root.entries) {
            const text = String(entry[root.textRole] ?? "").toLowerCase();
            if (text.indexOf(needle) >= 0) {
                result.push(entry);
            }
        }
        return result;
    }

    QQC2.TextField {
        id: searchField

        objectName: "filteredComboBoxSearch"
        Layout.fillWidth: true
        placeholderText: root.searchPlaceholder
        Accessible.name: root.searchPlaceholder
        // 清空搜索：让用户能一键回到完整列表
        rightPadding: Kirigami.Units.gridUnit * 2
        onTextChanged: combo.currentIndex = -1

        QQC2.ToolButton {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            visible: searchField.text.length > 0
            icon.name: "edit-clear"
            flat: true
            Accessible.name: i18n("Clear the search")
            onClicked: searchField.text = ""
        }
    }

    QQC2.ComboBox {
        id: combo

        objectName: "filteredComboBoxList"
        Layout.fillWidth: true
        model: root.filteredEntries
        textRole: root.textRole
        displayText: currentIndex >= 0 ? currentText : root.placeholder
        enabled: root.filteredEntries.length > 0
        onActivated: function (index) {
            const entry = root.filteredEntries[index];
            if (entry !== undefined) {
                root.selected(entry);
            }
        }
    }

    QQC2.Label {
        objectName: "filteredComboBoxEmpty"
        Layout.fillWidth: true
        visible: root.filteredEntries.length === 0
        text: i18n("Nothing matches the search.")
        font: Kirigami.Theme.smallFont
        opacity: 0.7
        wrapMode: Text.WordWrap
    }
}
