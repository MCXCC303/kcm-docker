/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    可折叠分区（ARCH_V3 §2.1 / ARCH_V2 §40）：

    二级标题 + 「默认隐藏」提示 + 展开后才渲染的内容。
    Environment / Labels 这类潜在敏感信息默认只显示数量（§40），
    展开动作只改本组件的 expanded，不触碰任何数据。

    这个结构（标题行 + 箭头 + 分隔线 + visible 控制的内容）原本在两个详情页里
    重复了三次，其中一次还是导致「内容与标题重叠」那个 bug 的来源——
    当时用的是 Kirigami.AbstractCard，它会接管 contentItem 的可见性与坐标
    （见 ARCH_V3 §四偏离登记）。这里改用可控结构：ItemDelegate 标题 +
    Kirigami.Separator + 由 visible 控制的 ColumnLayout。

    用法：

        Components.CollapsibleSection {
            title: i18ncp(...)
            contentObjectName: "environmentValues"
            Components.KeyValueList { model: page.controller.environmentVariables }
        }
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

ColumnLayout {
    id: control

    /*! 分区标题（含数量等运行时信息）。 */
    required property string title

    /*!
        展开后内容容器的 objectName。
        测试要靠它断言「默认折叠」（ARCH_V2 §40 / ARCH_V3 §5.1）。
    */
    property string contentObjectName: ""

    /*! 是否展开；默认折叠。 */
    property bool expanded: false

    /*! 展开后渲染的内容（默认属性）。 */
    default property alias content: contentLayout.data

    spacing: 0

    QQC2.ItemDelegate {
        Layout.fillWidth: true
        text: control.title
        onClicked: control.expanded = !control.expanded

        contentItem: RowLayout {
            spacing: Kirigami.Units.smallSpacing

            Kirigami.Icon {
                source: control.expanded ? "arrow-down" : "arrow-right"
                implicitWidth: Kirigami.Units.iconSizes.small
                implicitHeight: Kirigami.Units.iconSizes.small
            }
            QQC2.Label {
                text: parent.parent.text
                Layout.fillWidth: true
            }
            QQC2.Label {
                visible: !control.expanded
                text: i18n("hidden by default")
                font: Kirigami.Theme.smallFont
                opacity: 0.6
            }
        }
    }

    Kirigami.Separator {
        Layout.fillWidth: true
    }

    // ColumnLayout 会忽略不可见的子项，因此折叠时既不占位也不会与标题重叠
    ColumnLayout {
        id: contentLayout

        objectName: control.contentObjectName
        visible: control.expanded
        Layout.fillWidth: true
        spacing: 0
    }
}
