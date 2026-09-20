/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Kontainer KCM root object (ARCH_V2 §5/§43): owns page navigation and KCM-level actions only.

    Navigation uses an in-page StackView (no reliance on host KCM push/pop support);
    backend and models know nothing about page structure (§43).
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
            Manual refresh.

            Deliberately not bound to `enabled: !controller.busy`: the 5-second auto-refresh toggles
            busy every tick, so the button would flicker between clickable and greyed out. Repeated
            refreshes are harmless anyway — the backend coalesces in-flight duplicate requests (ARCH_V2 §29).
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
            Auto-refresh toggle (ARCH_V3 §2.7).

            When off, nothing polls in the background: the UI updates only when the user presses
            Refresh. That is both a user preference (a config panel need not keep churning) and a
            diagnostic switch for "UI rebuilt by the timer" bugs.
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
        // No fancy page transitions: steadiness inside a KCM matters more
        pushEnter: Transition {}
        pushExit: Transition {}
        popEnter: Transition {}
        popExit: Transition {}

        Component {
            id: mainPageComponent

            MainPage {
                // Debug: KCM_DOCKER_START_TAB=<index>; the controller reads the env var because QML cannot
                startTab: kcm.controller.startTabFromEnvironment
                onContainerActivated: function (containerId) {
                    stack.push(containerDetailComponent, {
                        "containerId": containerId
                    });
                }
                // The ports page "jump" lands on the same detail page as the container list
                onPortContainerActivated: function (containerId) {
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
                onCreateContainerRequested: function (presetImage) {
                    stack.push(createContainerComponent, {
                        "presetImage": presetImage
                    });
                }
                onVolumeActivated: function (volumeName) {
                    stack.push(volumeDetailComponent, {
                        "volumeName": volumeName
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
                onImageRequested: function (imageId) {
                    stack.push(imageDetailComponent, {
                        "imageId": imageId
                    });
                }
                onCloneRequested: function (containerId) {
                    stack.push(createContainerComponent, {
                        "cloneFromContainerId": containerId
                    });
                }
            }
        }

        Component {
            id: createContainerComponent

            CreateContainer {
                onCloseRequested: stack.pop()
                // Created successfully: go straight to the new container's detail page (§4.6)
                onContainerCreated: function (containerId) {
                    stack.pop();
                    stack.push(containerDetailComponent, {
                        "containerId": containerId
                    });
                }
            }
        }

        Component {
            id: volumeDetailComponent

            VolumeDetail {
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
                onContainerRequested: function (containerId) {
                    stack.push(containerDetailComponent, {
                        "containerId": containerId
                    });
                }
                onCreateContainerRequested: function (imageReference) {
                    stack.push(createContainerComponent, {
                        "presetImage": imageReference
                    });
                }
                onRegistryAuthRequested: function (serverAddress) {
                    stack.push(registryAuthComponent, {
                        "presetServerAddress": serverAddress
                    });
                }
            }
        }
    }
}
