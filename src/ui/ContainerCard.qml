/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    容器卡片（ARCH_V2 §6）：整张卡片是一个可点击导航区域。

    - 使用 QQC2.ItemDelegate：自带 hover / focus / Enter / Space 行为（§6.2/§38）
    - 卡片内不放任何 Start/Stop/Restart 按钮（二期禁止 mutation）
    - 状态同时用文本 + 图标 + 颜色表达（§12）
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.kontainer as Kontainer

QQC2.ItemDelegate {
    id: card

    objectName: "containerCard"

    /*! 模型角色：缺少 required 声明时，delegate 内的标识符无法解析
        （点击时会抛 ReferenceError: containerId is not defined）。 */
    required property string containerId
    required property string name
    required property string shortId
    required property string image
    required property string stateKey
    required property string stateText
    required property string status
    required property string healthKey
    required property string healthText
    required property string portsSummary
    required property var created

    signal activated

    width: ListView.view ? ListView.view.width : implicitWidth
    hoverEnabled: true

    onClicked: card.activated()

    Accessible.name: i18n("Container %1, %2", card.name, card.stateText)
    Accessible.description: card.image
    Accessible.role: Accessible.ListItem

    contentItem: RowLayout {
        spacing: Kirigami.Units.smallSpacing

        Kirigami.Icon {
            source: card.stateIcon(card.stateKey)
            color: card.stateColor(card.stateKey, card.healthKey)
            implicitWidth: Kirigami.Units.iconSizes.smallMedium
            implicitHeight: Kirigami.Units.iconSizes.smallMedium
            Layout.alignment: Qt.AlignTop
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

            RowLayout {
                spacing: Kirigami.Units.smallSpacing

                QQC2.Label {
                    text: card.name
                    font.bold: true
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                QQC2.Label {
                    text: card.stateText
                    color: card.stateColor(card.stateKey, card.healthKey)
                    font: Kirigami.Theme.smallFont
                }
                RowLayout {
                    visible: card.healthKey === "healthy" || card.healthKey === "unhealthy" || card.healthKey === "starting"
                    spacing: Kirigami.Units.smallSpacing / 2

                    Kirigami.Icon {
                        source: card.healthIcon(card.healthKey)
                        color: card.stateColor(card.stateKey, card.healthKey)
                        implicitWidth: Kirigami.Units.iconSizes.small
                        implicitHeight: Kirigami.Units.iconSizes.small
                    }
                    QQC2.Label {
                        text: card.healthText
                        font: Kirigami.Theme.smallFont
                        opacity: 0.8
                    }
                }
            }

            QQC2.Label {
                Layout.fillWidth: true
                text: card.image
                elide: Text.ElideMiddle
                font: Kirigami.Theme.smallFont
                opacity: 0.8
            }

            QQC2.Label {
                Layout.fillWidth: true
                visible: text.length > 0
                text: i18nc("@info container status line", "%1 · created %2 ago · ID %3", card.status, Kontainer.Format.elapsed(card.created), card.shortId)
                elide: Text.ElideRight
                font: Kirigami.Theme.smallFont
                opacity: 0.6
            }

            QQC2.Label {
                Layout.fillWidth: true
                visible: card.portsSummary.length > 0
                text: i18nc("@info published container ports", "Ports: %1", card.portsSummary)
                elide: Text.ElideRight
                font: Kirigami.Theme.smallFont
                opacity: 0.6
            }
        }

        Kirigami.Icon {
            source: "go-next-symbolic"
            implicitWidth: Kirigami.Units.iconSizes.small
            implicitHeight: Kirigami.Units.iconSizes.small
            opacity: 0.5
            Layout.alignment: Qt.AlignVCenter
        }
    }

    /* 语义色 → Kirigami palette（颜色只作辅助，文本与图标始终同时存在，§12） */
    function stateColor(stateKey: string, healthKey: string): color {
        switch (Kontainer.Presentation.stateSemanticKey(stateKey, healthKey)) {
        case "positive":
            return Kirigami.Theme.positiveTextColor;
        case "neutral":
            return Kirigami.Theme.neutralTextColor;
        case "negative":
            return Kirigami.Theme.negativeTextColor;
        default:
            return Kirigami.Theme.disabledTextColor;
        }
    }

    function stateIcon(stateKey: string): string {
        return Kontainer.Presentation.stateIconName(stateKey);
    }

    function healthIcon(healthKey: string): string {
        return Kontainer.Presentation.healthIconName(healthKey);
    }
}
