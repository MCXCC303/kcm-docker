/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Engine info view (ARCH_V2 §5: engine details live here; the page header keeps only a compact status row).
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

ColumnLayout {
    id: view

    required property var engine

    /*! Build stamp, supplied by StatusController. */
    required property string buildStamp

    spacing: Kirigami.Units.smallSpacing

    Kirigami.FormLayout {
        Layout.fillWidth: true

        QQC2.Label {
            Kirigami.FormData.label: i18n("Endpoint:")
            text: view.engine && view.engine.engineName ? view.engine.engineName : i18n("Unknown")
            visible: false // engine name is already shown in the page header
        }

        QQC2.Label {
            Kirigami.FormData.label: i18n("Engine version:")
            text: view.engine.serverVersion.length > 0 ? view.engine.serverVersion : i18n("Unknown")
        }

        QQC2.Label {
            Kirigami.FormData.label: i18n("API version:")
            text: {
                if (view.engine.apiVersion.length === 0) {
                    return i18n("Unknown");
                }
                if (view.engine.minApiVersion.length > 0) {
                    return i18nc("%1 is the API version in use, %2 the minimum version the daemon offers", "%1 (daemon minimum %2)", view.engine.apiVersion, view.engine.minApiVersion);
                }
                return view.engine.apiVersion;
            }
        }

        QQC2.Label {
            Kirigami.FormData.label: i18n("Operating system:")
            visible: text.length > 0
            text: {
                const os = view.engine.operatingSystem;
                const arch = view.engine.architecture;
                if (os.length === 0) {
                    return arch;
                }
                return arch.length > 0 ? i18nc("%1 operating system, %2 architecture", "%1 (%2)", os, arch) : os;
            }
        }

        QQC2.Label {
            Kirigami.FormData.label: i18n("Kernel:")
            visible: text.length > 0
            text: view.engine.kernelVersion
        }

        QQC2.Label {
            Kirigami.FormData.label: i18n("Cgroup version:")
            visible: text.length > 0
            text: view.engine.cgroupVersion
        }

        QQC2.Label {
            Kirigami.FormData.label: i18n("Storage driver:")
            visible: text.length > 0
            text: view.engine.storageDriver
        }

        QQC2.Label {
            Kirigami.FormData.label: i18n("Cgroup driver:")
            visible: text.length > 0
            text: view.engine.cgroupDriver
        }

        QQC2.Label {
            objectName: "engineCpuCount"
            Kirigami.FormData.label: i18n("CPUs:")
            visible: view.engine.cpuCount > 0
            text: String(view.engine.cpuCount)
        }

        /* Component versions (dockerd / containerd / runc / docker-init …): the engine version
           describes dockerd only, while containerd and runc versions change behavior too. */
        Repeater {
            model: view.engine.components

            delegate: QQC2.Label {
                required property var modelData

                objectName: "engineComponentRow"
                Kirigami.FormData.label: i18nc("@item %1 is a component name such as containerd", "%1 version:", modelData.name)
                text: modelData.version
            }
        }
    }

    /* Problems the engine reports about itself (swap limits, iptables): shown verbatim, never reworded */
    Kirigami.InlineMessage {
        objectName: "engineWarnings"
        Layout.fillWidth: true
        visible: view.engine.warnings.length > 0
        type: Kirigami.MessageType.Warning
        text: view.engine.warnings.join("\n")
    }

    /* Build stamp: tells which build is running when troubleshooting (ARCH_V3 appendix A.1f). */
    QQC2.Label {
        Layout.fillWidth: true
        Layout.topMargin: Kirigami.Units.smallSpacing
        text: i18nc("@info build identification", "Build: %1", view.buildStamp)
        font: Kirigami.Theme.smallFont
        opacity: 0.6
        wrapMode: Text.WordWrap
    }
}
