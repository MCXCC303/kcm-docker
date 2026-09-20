/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Copy button (ARCH_V3 §2.1): the copy **action** for identifier fields lives here only.

    This used to be hand-written six times across two detail pages (icon name, tooltip,
    accessible name, disable-on-empty, clipboard call), so every change meant six edits.

    Identifier fields only (name / ID / reference / path); there is still no
    "copy the whole inspect JSON" button (ARCH_V2 §41).

    Two usages:
      - field row: use CopyableText directly (= value + this button)
      - custom layouts such as a page title: place this button alone
*/

import QtQuick
import QtQuick.Controls as QQC2

import org.kde.kirigami as Kirigami
import org.kde.kcm.docker as Kontainer

QQC2.ToolButton {
    id: button

    /*! Value to write to the clipboard. */
    required property string value

    /*! Name of the field in accessible and tooltip texts, e.g. "container ID". */
    property string fieldLabel: ""

    /*! Copy succeeded. */
    signal copied

    readonly property bool hasValue: button.value.length > 0
    readonly property string actionText: button.fieldLabel.length > 0 ? i18nc("@info copy a named field", "Copy %1", button.fieldLabel) : i18n("Copy")

    icon.name: "edit-copy"
    display: QQC2.AbstractButton.IconOnly
    enabled: button.hasValue

    QQC2.ToolTip.text: button.actionText
    QQC2.ToolTip.visible: hovered
    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

    Accessible.name: button.actionText

    onClicked: {
        Kontainer.Presentation.copyToClipboard(button.value);
        button.copied();
    }
}
