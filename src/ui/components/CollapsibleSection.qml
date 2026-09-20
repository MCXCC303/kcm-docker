/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Collapsible section (ARCH_V3 §2.1 / ARCH_V2 §40):

    Sub-heading + "hidden by default" hint + content rendered only once expanded.
    Potentially sensitive data such as Environment / Labels shows only a count by default
    (§40); expanding toggles this component's `expanded` and touches no data.

    This structure (title row + arrow + separator + visible-controlled content) was
    triplicated across two detail pages, and one copy caused the "content overlaps title"
    bug: Kirigami.AbstractCard takes over the contentItem's visibility and coordinates
    (ARCH_V3 §deviations). Here it is rebuilt from controllable parts: ItemDelegate title
    + Kirigami.Separator + a ColumnLayout gated by visible.

    Usage:

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

    /*! Section title (includes runtime information such as counts). */
    required property string title

    /*!
        objectName of the expanded content container; tests assert "collapsed by default"
        through it (ARCH_V2 §40 / ARCH_V3 §5.1).
    */
    property string contentObjectName: ""

    /*! Whether the section is expanded; collapsed by default. */
    property bool expanded: false

    /*! Content rendered when expanded (default property). */
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
        }
    }

    Kirigami.Separator {
        Layout.fillWidth: true
    }

    // ColumnLayout skips invisible children, so collapsed content neither takes space nor overlaps the title
    ColumnLayout {
        id: contentLayout

        objectName: control.contentObjectName
        visible: control.expanded
        Layout.fillWidth: true
        spacing: 0
    }
}
