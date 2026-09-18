/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    挂载预设管理（ARCH_V5_V8 §4.2）。

    用户实测反馈："预设管理建议单独开一个标签页，在创建容器中进行预设管理不是很方便"——
    因此把管理界面从创建向导里搬到这里（主页面「挂载预设」标签页），向导里只留"快速添加"。

    每一条可改宿主/容器路径、切换收藏、上移/下移、删除；底部一行可新增。
    编辑直接写回存储（失焦即保存），因为预设是"这个工具的数据"而不是待提交的表单。
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

import "." as Local

ColumnLayout {
    id: root

    /*! 预设存储（`kcm.controller.mountPresets`）。 */
    required property var store

    /*!
     * delegate 用的中转对象（Unbound 下 delegate 拿不到根对象 id，见 CreateContainer.qml）。
     */
    QtObject {
        id: manager

        readonly property var store: root.store

        function remove(presetId) {
            manager.store.remove(presetId);
        }
        function toggleFavorite(presetId, favorite) {
            manager.store.setFavorite(presetId, favorite);
        }
        function moveUp(presetId) {
            manager.store.moveUp(presetId);
        }
        function moveDown(presetId) {
            manager.store.moveDown(presetId);
        }
        function update(presetId, source, destination, readOnly, note) {
            manager.store.update(presetId, source, destination, readOnly, note);
        }
        function add(source, destination) {
            return manager.store.add(source, destination, "bind", false, "");
        }
    }

    objectName: "mountPresetManager"
    spacing: Kirigami.Units.smallSpacing

    EmptyPlaceholder {
        objectName: "presetManagerEmpty"
        Layout.fillWidth: true
        message: root.store.empty
            ? i18n("No presets yet. Add one below, or save a mount as a preset from a container's details.")
            : ""
    }

    Repeater {
        // 必须用**属性**（summaries 有 NOTIFY）：函数调用不建立依赖，
        // 否则新增/删除预设时列表不会重铺
        model: root.store.summaries

        delegate: Kirigami.AbstractCard {
            id: presetCard

            required property string id
            required property string source
            required property string destination
            required property string type
            required property bool readOnly
            required property string note
            required property bool favorite

            objectName: "presetManagerRow"
            Layout.fillWidth: true
            showClickFeedback: false

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing / 2

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.TextField {
                        objectName: "presetManagerSource"
                        Layout.fillWidth: true
                        placeholderText: i18n("Host path or volume name")
                        Accessible.name: i18n("Preset source")
                        text: presetCard.source
                        onEditingFinished: manager.update(presetCard.id, text, presetCard.destination, presetCard.readOnly, presetCard.note)
                    }

                    QQC2.Label {
                        text: "→"
                        opacity: 0.6
                    }

                    QQC2.TextField {
                        objectName: "presetManagerDestination"
                        Layout.fillWidth: true
                        placeholderText: i18n("Container path")
                        Accessible.name: i18n("Preset destination")
                        text: presetCard.destination
                        onEditingFinished: manager.update(presetCard.id, presetCard.source, text, presetCard.readOnly, presetCard.note)
                    }

                    QQC2.CheckBox {
                        objectName: "presetManagerReadOnly"
                        text: i18n("Read-only")
                        checked: presetCard.readOnly
                        onToggled: manager.update(presetCard.id, presetCard.source, presetCard.destination, checked, presetCard.note)
                    }

                    QQC2.CheckBox {
                        objectName: "presetManagerFavorite"
                        text: i18n("Favourite")
                        checked: presetCard.favorite
                        onToggled: manager.toggleFavorite(presetCard.id, checked)
                    }

                    QQC2.ToolButton {
                        objectName: "presetManagerMoveUp"
                        icon.name: "go-up"
                        Accessible.name: i18n("Move up")
                        onClicked: manager.moveUp(presetCard.id)
                    }

                    QQC2.ToolButton {
                        objectName: "presetManagerMoveDown"
                        icon.name: "go-down"
                        Accessible.name: i18n("Move down")
                        onClicked: manager.moveDown(presetCard.id)
                    }

                    QQC2.ToolButton {
                        objectName: "presetManagerRemove"
                        icon.name: "edit-delete"
                        Accessible.name: i18n("Remove this preset")
                        onClicked: manager.remove(presetCard.id)
                    }
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    visible: presetCard.type === "volume"
                    text: i18n("Named volume")
                    font: Kirigami.Theme.smallFont
                    opacity: 0.7
                }
            }

            Accessible.name: i18n("%1 to %2", presetCard.source, presetCard.destination)
        }
    }

    /* ------------------------------ 新增 ------------------------------ */
    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        QQC2.TextField {
            id: newSource

            objectName: "presetManagerNewSource"
            Layout.fillWidth: true
            placeholderText: i18n("Host path or volume name")
            Accessible.name: i18n("New preset source")
        }

        QQC2.TextField {
            id: newDestination

            objectName: "presetManagerNewDestination"
            Layout.fillWidth: true
            placeholderText: i18n("Container path")
            Accessible.name: i18n("New preset destination")
        }

        QQC2.Button {
            objectName: "presetManagerAdd"
            text: i18n("Add")
            icon.name: "list-add"
            enabled: newSource.text.trim().length > 0 && newDestination.text.trim().length > 0
            onClicked: {
                const created = manager.add(newSource.text, newDestination.text);
                if (created.length > 0) {
                    newSource.text = "";
                    newDestination.text = "";
                }
            }
        }
    }

    // 校验规则与存储共用一份实现：非法输入会被拒绝，这里把原因说出来
    Kirigami.InlineMessage {
        objectName: "presetManagerError"
        Layout.fillWidth: true
        visible: message.length > 0
        type: Kirigami.MessageType.Error
        property string message: {
            if (newSource.text.trim().length === 0 && newDestination.text.trim().length === 0) {
                return "";
            }
            const sourceError = root.store.sourceError(newSource.text, "bind");
            if (sourceError === "sourceNotAbsolute") {
                return i18n("Bind mounts need an absolute host path (use a volume name for a named volume).");
            }
            const destinationError = root.store.destinationError(newDestination.text);
            if (destinationError === "destinationNotAbsolute") {
                return i18n("The container path must be absolute.");
            }
            return "";
        }
    }
}
