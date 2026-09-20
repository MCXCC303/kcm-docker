/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Image reference input (ARCH_V5_V8 §1.6).

    Used for pulling images, creating containers and Dockerfile build tags — with
    **validation implemented once**: no regex here, it calls C++ `isValidImageReference` /
    `normalizedImageReference` (implemented and tested in `ImageReference` back in phase 4).

    Usage:

        Components.ImageRefInput {
            operations: root.operations        // provides the two validation methods
            placeholderText: i18n("alpine:3.19")
            onAccepted: root.startPull(text)
        }

    Public surface: `text` (read/write), `referenceValid`, `normalizedReference`,
    `plainReference` (a missing tag gets `latest`), `alreadyPulling`, signal `accepted`.
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

import "." as Local

ColumnLayout {
    id: root

    objectName: "imageRefInput"

    /*! Object providing `isValidImageReference` / `normalizedImageReference` / `pulls`. */
    required property var operations

    /*! Current input text (read and written by the caller). */
    property alias text: field.text
    /*! Field placeholder text. */
    property string placeholderText: i18n("alpine:3.19")
    /*! Whether to show the "already pulling" hint (only the pull scenario needs it). */
    property bool checkAlreadyPulling: true

    /*! Whether the text is a valid image reference. */
    readonly property bool referenceValid: root.operations.isValidImageReference(root.text)
    /*! Registry address of the input (empty when invalid); the pull dialog warns "not logged in" with it. */
    readonly property string serverAddress: root.referenceValid ? root.operations.serverAddressForImage(root.text) : ""
    /*! Normalised reference (a missing tag gets `latest`). */
    readonly property string normalizedReference: root.operations.normalizedImageReference(root.text)
    /*!
     * Normalisation changed the reference (the user typed no tag) → say explicitly what
     * will be pulled. Note the `trim()`: Qt 6 QML JS no longer exposes QString methods on
     * strings (`trimmed()` throws a TypeError).
     */
    readonly property bool plainReference: root.referenceValid && root.text.trim() !== root.normalizedReference
    /*!
     * This reference is in the in-progress pull list.
     *
     * `count > 0` is not redundant: `rowForReference` is Q_INVOKABLE and QML does not track
     * function calls, so the binding also needs a notifiable property (the row count) to
     * re-evaluate when pulls start or finish.
     */
    readonly property bool alreadyPulling: root.checkAlreadyPulling && root.referenceValid
        && root.operations.pulls.count > 0
        && root.operations.pulls.rowForReference(root.normalizedReference) >= 0
    /*! Whether submission is allowed (valid and not a duplicate pull). */
    readonly property bool acceptable: root.referenceValid && !root.alreadyPulling

    /*! The user pressed Enter (or confirmed after `forceActiveFocus()`). */
    signal accepted

    spacing: Kirigami.Units.smallSpacing

    function forceActiveFocus() {
        field.forceActiveFocus();
    }

    QQC2.TextField {
        id: field

        objectName: "imageRefField"
        Layout.fillWidth: true
        placeholderText: root.placeholderText
        onAccepted: root.accepted()
    }

    QQC2.Label {
        objectName: "imageRefError"
        Layout.fillWidth: true
        visible: field.text.length > 0 && !root.referenceValid
        text: i18n("This is not a valid image reference.")
        // Negative colour only via StatusPalette (single source of status colours, §12)
        color: Local.StatusPalette.color("negative")
        wrapMode: Text.WordWrap
        font: Kirigami.Theme.smallFont
    }

    QQC2.Label {
        objectName: "imageRefLatestHint"
        Layout.fillWidth: true
        visible: root.referenceValid && root.plainReference
        text: i18n("No tag given, “latest” will be pulled: %1", root.normalizedReference)
        wrapMode: Text.WordWrap
        font: Kirigami.Theme.smallFont
        opacity: 0.8
    }

    QQC2.Label {
        objectName: "imageRefAlreadyPullingHint"
        Layout.fillWidth: true
        visible: root.alreadyPulling
        text: i18n("This image is already being pulled.")
        wrapMode: Text.WordWrap
        font: Kirigami.Theme.smallFont
        opacity: 0.8
    }

    Accessible.name: i18n("Image reference")
    Accessible.description: root.text
}
