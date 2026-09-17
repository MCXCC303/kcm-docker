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
        /*!
            手动刷新。

            注意：这里**不**绑定 `enabled: !controller.busy`。自动刷新每 5 秒会让
            busy 抖动一次，按钮跟着在「可点 / 不可点」之间切换，看起来就是在闪；
            而重复触发刷新本身是无害的——backend 会把在途的同类请求合并（ARCH_V2 §29）。
        */
        Kirigami.Action {
            text: i18n("Refresh")
            icon.name: "view-refresh"
            visible: stack.depth === 1
            onTriggered: root.controller.refresh()
        },
        Kirigami.Action {
            text: i18n("Back")
            icon.name: "go-previous"
            visible: stack.depth > 1
            onTriggered: stack.pop()
        },
        /*!
            自动刷新开关（ARCH_V3 §2.7）。

            关掉之后后台不再有任何定时刷新：界面只在用户按「刷新」时更新。
            这既是给用户的选择（配置面板不需要一直跳动），
            也是排查「定时刷新触发的界面重建」类问题的诊断开关。
        */
        Kirigami.Action {
            text: i18n("Auto-refresh")
            icon.name: "view-refresh"
            checkable: true
            checked: root.controller.autoRefreshEnabled
            onTriggered: root.controller.autoRefreshEnabled = !root.controller.autoRefreshEnabled
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
                onConfigureRuntimeRequested: function (scope) {
                    stack.push(daemonConfigComponent, {
                        "scope": scope
                    });
                }
                onNetworkActivated: function (networkId) {
                    stack.push(networkDetailComponent, {
                        "networkId": networkId
                    });
                }
                onRegistryAuthRequested: function (serverAddress) {
                    stack.push(registryAuthComponent, {
                        "presetServerAddress": serverAddress
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
            id: registryAuthComponent

            RegistryAuthPage {
                onCloseRequested: stack.pop()
            }
        }

        Component {
            id: daemonConfigComponent

            DaemonConfigPage {
                scope: "system"
                onCloseRequested: stack.pop()
            }
        }

        Component {
            id: networkDetailComponent

            NetworkDetail {
                onCloseRequested: stack.pop()
                onContainerRequested: function (containerId) {
                    stack.push(containerDetailComponent, {
                        "containerId": containerId
                    });
                }
            }
        }

        Component {
            id: imageDetailComponent

            ImageDetail {
                onCloseRequested: stack.pop()
                onRegistryAuthRequested: function (serverAddress) {
                    stack.push(registryAuthComponent, {
                        "presetServerAddress": serverAddress
                    });
                }
            }
        }
    }
}
