/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    容器卡片（ARCH_V2 §6 / ARCH_V3 §2.1 / ARCH_V4 §2.3）：整张卡片是一个可点击导航区域。

    - 使用 QQC2.ItemDelegate：自带 hover / focus / Enter / Space 行为（§6.2/§38）
    - 行内只放**可逆**操作（启动 / 停止）；删除只在详情页 footer，减少误触面（ARCH_V4 §2.3）
    - 写入口只在 socket 允许写时出现（权限门），忙碌时禁用并显示进度指示
    - 状态用统一的 StatusChip 呈现（图标 + 颜色 + 文字三重编码，§12）
    - 复制入口：只复制标识（容器 ID），不复制整个 inspect JSON（ARCH_V2 §41）
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.kontainer as Kontainer

import "components" as Components

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

    /*! 写操作控制器（由 MainPage 传入：组件不自己去找 kcm）。 */
    required property var operations

    signal activated

    width: ListView.view ? ListView.view.width : implicitWidth
    hoverEnabled: true

    onClicked: card.activated()

    Accessible.name: i18n("Container %1, %2", card.name, card.stateText)
    Accessible.description: card.image
    Accessible.role: Accessible.ListItem

    /*! 状态语义：健康问题优先于状态（Unhealthy 的 Running 必须看起来有问题，§11.3） */
    readonly property string stateSemanticKey: Kontainer.Presentation.stateSemanticKey(card.stateKey, card.healthKey)
    readonly property bool healthVisible: card.healthKey === "healthy" || card.healthKey === "unhealthy" || card.healthKey === "starting"
    /*! 这个容器是否有操作在途（启动 / 停止 / 重启 / 删除都算）。 */
    readonly property bool targetBusy: {
        // Q_INVOKABLE 调用不会被 QML 追踪：先读一次可通知属性，忙碌状态变化时才能重新求值
        card.operations.stateRevision;
        return card.operations.isContainerBusy(card.containerId);
    }
    readonly property bool canStart: card.operations.writeAllowed && !card.targetBusy
        && (card.stateKey === "exited" || card.stateKey === "created" || card.stateKey === "dead")
    readonly property bool canStop: card.operations.writeAllowed && !card.targetBusy
        && (card.stateKey === "running" || card.stateKey === "paused" || card.stateKey === "restarting")

    contentItem: RowLayout {
        spacing: Kirigami.Units.smallSpacing

        Kirigami.Icon {
            source: Kontainer.Presentation.stateIconName(card.stateKey)
            color: Components.StatusPalette.color(card.stateSemanticKey)
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

                Components.StatusChip {
                    semanticKey: card.stateSemanticKey
                    iconName: Kontainer.Presentation.stateIconName(card.stateKey)
                    text: card.stateText
                }

                RowLayout {
                    visible: card.healthVisible
                    spacing: Kirigami.Units.smallSpacing / 2

                    Kirigami.Icon {
                        source: Kontainer.Presentation.healthIconName(card.healthKey)
                        color: Components.StatusPalette.color(card.stateSemanticKey)
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

        // 行内生命周期操作（ARCH_V4 §2.3）：可逆操作放列表，破坏性操作放详情页。
        // 按钮自己接受鼠标事件，因此不会触发卡片导航。
        QQC2.BusyIndicator {
            objectName: "containerBusyIndicator"
            visible: card.targetBusy
            running: card.targetBusy
            implicitWidth: Kirigami.Units.iconSizes.smallMedium
            implicitHeight: Kirigami.Units.iconSizes.smallMedium
            Layout.alignment: Qt.AlignVCenter
        }

        QQC2.ToolButton {
            objectName: "containerStartButton"
            visible: card.canStart
            icon.name: "media-playback-start"
            text: i18n("Start")
            display: QQC2.AbstractButton.IconOnly
            Layout.alignment: Qt.AlignVCenter
            Accessible.name: i18n("Start container %1", card.name)
            QQC2.ToolTip.text: i18n("Start")
            QQC2.ToolTip.visible: hovered
            onClicked: card.operations.startContainer(card.containerId)
        }

        QQC2.ToolButton {
            objectName: "containerStopButton"
            visible: card.canStop
            icon.name: "media-playback-stop"
            text: i18n("Stop")
            display: QQC2.AbstractButton.IconOnly
            Layout.alignment: Qt.AlignVCenter
            Accessible.name: i18n("Stop container %1", card.name)
            QQC2.ToolTip.text: i18n("Stop")
            QQC2.ToolTip.visible: hovered
            onClicked: card.operations.stopContainer(card.containerId)
        }

        // 列表里的复制入口（§1.3）：复制容器 ID——点击本按钮不会触发卡片导航，
        // 因为 AbstractButton 会接受鼠标事件，不再向父 delegate 传播。
        Components.CopyButton {
            value: card.containerId
            fieldLabel: i18n("container ID")
            Layout.alignment: Qt.AlignVCenter
        }

        Kirigami.Icon {
            source: "go-next-symbolic"
            implicitWidth: Kirigami.Units.iconSizes.small
            implicitHeight: Kirigami.Units.iconSizes.small
            opacity: 0.5
            Layout.alignment: Qt.AlignVCenter
        }
    }
}
