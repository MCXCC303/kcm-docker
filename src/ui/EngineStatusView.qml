/*
    SPDX-FileCopyrightText: 2026 kontainer developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Engine 信息视图（ARCH_V2 §5：Engine 详情从页头移到这里，页头只保留紧凑状态行）。
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

ColumnLayout {
    id: view

    required property var engine

    /*! 构建标记，由 StatusController 提供。 */
    required property string buildStamp

    spacing: Kirigami.Units.smallSpacing

    Kirigami.FormLayout {
        Layout.fillWidth: true

        QQC2.Label {
            Kirigami.FormData.label: i18n("Endpoint:")
            text: view.engine && view.engine.engineName ? view.engine.engineName : i18n("Unknown")
            visible: false // engine name 已由页头展示
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
    }

    /* 构建标记：排查问题时用来确认运行的是哪一次构建（ARCH_V3 附录 A.1f）。 */
    QQC2.Label {
        Layout.fillWidth: true
        Layout.topMargin: Kirigami.Units.smallSpacing
        text: i18nc("@info build identification", "Build: %1", view.buildStamp)
        font: Kirigami.Theme.smallFont
        opacity: 0.6
        wrapMode: Text.WordWrap
    }
}
