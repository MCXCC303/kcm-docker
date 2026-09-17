/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    数据卷卡片（ARCH_V5_V8 §3.5）：整张卡片可点击进入卷详情。

    占用与引用数可能是**未知**（引擎没扫）：未知显示「—」而不是 0——
    "0 字节"和"不知道"在"能不能安全清理"这件事上是完全不同的答案。
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.kontainer as Kontainer

import "components" as Components

QQC2.ItemDelegate {
    id: card

    objectName: "volumeCard"

    required property string name
    required property string driver
    required property string mountpoint
    required property bool sizeKnown
    required property double sizeBytes
    required property bool usageKnown
    required property int refCount

    signal activated

    width: ListView.view ? ListView.view.width : implicitWidth
    hoverEnabled: true
    onClicked: card.activated()

    Accessible.name: i18n("Volume %1", card.name)
    Accessible.role: Accessible.ListItem

    contentItem: RowLayout {
        spacing: Kirigami.Units.smallSpacing

        Kirigami.Icon {
            source: "drive-harddisk"
            // 使用情况未知时不着色（未知不是错误，也不是"空闲"）
            color: card.usageKnown && !card.refCount ? Components.StatusPalette.color("neutral") : Kirigami.Theme.textColor
            implicitWidth: Kirigami.Units.iconSizes.smallMedium
            implicitHeight: Kirigami.Units.iconSizes.smallMedium
            Layout.alignment: Qt.AlignTop
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing / 2

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                QQC2.Label {
                    objectName: "volumeNameLabel"
                    text: card.name
                    font.bold: true
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }

                Components.FieldChip {
                    objectName: "volumeDriverChip"
                    muted: true
                    text: card.driver
                }

                Components.FieldChip {
                    objectName: "volumeInUseChip"
                    visible: card.usageKnown && card.refCount > 0
                    muted: true
                    text: i18np("used by %1 container", "used by %1 containers", card.refCount)
                }

                Components.FieldChip {
                    objectName: "volumeUnusedChip"
                    visible: card.usageKnown && card.refCount === 0
                    muted: true
                    text: i18n("unused")
                }

                Components.FieldChip {
                    objectName: "volumeUnknownUsageChip"
                    visible: !card.usageKnown
                    muted: true
                    text: i18n("usage unknown")
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                QQC2.Label {
                    objectName: "volumeMountpointLabel"
                    Layout.fillWidth: true
                    text: card.mountpoint
                    font.family: "monospace"
                    opacity: 0.8
                    elide: Text.ElideMiddle
                }

                QQC2.Label {
                    objectName: "volumeSizeLabel"
                    // 未知显示 —（不是 0）
                    text: card.sizeKnown ? Kontainer.Format.byteSize(card.sizeBytes) : i18n("—")
                    opacity: 0.8
                }
            }
        }
    }
}
