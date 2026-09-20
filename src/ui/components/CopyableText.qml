/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Copyable field row (ARCH_V3 §2.1 / ARCH_V3_pre §1.3):

    One implementation of "value + copy button" for identifier fields: container ID,
    image repository/tag/full reference/image ID, paths. The copy action itself is in
    CopyButton; this component only lays out.

    An empty value disables the button instead of copying an empty string.

    Usage (inside a Kirigami.FormLayout, used directly as the label row):

        Components.CopyableText {
            Kirigami.FormData.label: i18n("Container ID:")
            fieldLabel: i18n("container ID")
            value: controller.shortId
            copyValue: controller.containerId   // show the short ID, copy the full one
        }
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

import "." as Local

RowLayout {
    id: control

    /*! Value to display. */
    required property string value

    /*!
        Value actually written to the clipboard; falls back to value when empty.
        For cases like "show the short ID, copy the full one": a full ID is too long for
        one row, but the copied value must stay complete.
    */
    property string copyValue: ""

    /*! Name of the field in accessible and tooltip texts, e.g. "container ID". */
    property string fieldLabel: ""

    /*! Whether to use a monospace font (on by default for ID / hash / path, §1.5). */
    property bool monospace: true

    /*! Text shown when the value is empty. */
    property string placeholderText: i18n("—")

    /*! Elide mode when too long (ignored while `wrap` is true). */
    property int elideMode: Text.ElideMiddle

    /*!
     * Whether to wrap (default no, single line with elision).
     *
     * Values like commands — one long line whose middle must not be lost — are better
     * wrapped: give the component a fixed-width field and long values fold inside it.
     */
    property bool wrap: false

    /*! Copy succeeded. */
    signal copied

    spacing: Kirigami.Units.smallSpacing

    readonly property string effectiveCopyValue: control.copyValue.length > 0 ? control.copyValue : control.value
    readonly property bool hasValue: control.effectiveCopyValue.length > 0

    QQC2.Label {
        id: valueLabel

        objectName: "copyableTextValue"
        Layout.fillWidth: true
        text: control.value.length > 0 ? control.value : control.placeholderText
        font.family: control.monospace ? "monospace" : Kirigami.Theme.defaultFont.family
        // Elision only makes sense for a real value: the placeholder "—" is short and must not be cut off
        wrapMode: control.wrap ? Text.WrapAnywhere : Text.NoWrap
        elide: (control.value.length > 0 && !control.wrap) ? control.elideMode : Text.ElideNone
        opacity: control.value.length > 0 ? 1.0 : 0.6

        HoverHandler {
            id: valueHover
        }

        // Show the full value on hover when truncated (§1.5: long repository addresses must not force
        // a wider card). This is the **full value that will be copied**, matching the button.
        QQC2.ToolTip.visible: valueHover.hovered && valueLabel.truncated
        QQC2.ToolTip.text: control.effectiveCopyValue
        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
    }

    Local.CopyButton {
        objectName: "copyButtonObject"
        value: control.effectiveCopyValue
        fieldLabel: control.fieldLabel
        onCopied: control.copied()
    }
}
