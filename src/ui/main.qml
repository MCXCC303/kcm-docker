/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Kontainer KCM 根对象（ARCH_V2 §5/§43）：只负责页面导航与 KCM 级动作。

    导航使用页内 StackView（不依赖宿主对 KCM push/pop 的支持），
    Backend / Model 完全不知道页面结构（§43）。
*/

import QtQuick
import QtQuick.Controls as QQC2

import org.kde.kcmutils as KCM
import org.kde.kirigami as Kirigami

KCM.AbstractKCM {
    id: root

    readonly property var controller: kcm.controller

    actions: [
        Kirigami.Action {
            text: i18n("Refresh")
            icon.name: "view-refresh"
            visible: stack.depth === 1
            enabled: !root.controller.busy
            onTriggered: root.controller.refresh()
        },
        Kirigami.Action {
            text: i18n("Back")
            icon.name: "go-previous"
            visible: stack.depth > 1
            onTriggered: stack.pop()
        }
    ]


    QQC2.StackView {
        id: stack

        objectName: "pageStack"

        anchors.fill: parent
        initialItem: mainPageComponent
        // 页面切换时不做花哨动画，保持 KCM 内的稳定感
        pushEnter: Transition {}
        pushExit: Transition {}
        popEnter: Transition {}
        popExit: Transition {}

        Component {
            id: mainPageComponent

            MainPage {
                onContainerActivated: function (containerId) {
                    stack.push(containerDetailComponent, {
                        "containerId": containerId
                    });
                }
                onImageActivated: function (imageId) {
                    stack.push(imageDetailComponent, {
                        "imageId": imageId
                    });
                }
            }
        }

        Component {
            id: containerDetailComponent

            ContainerDetail {
                onCloseRequested: stack.pop()
            }
        }

        Component {
            id: imageDetailComponent

            ImageDetail {
                onCloseRequested: stack.pop()
            }
        }
    }
}
