/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Image Detail (ARCH_V2 §8/§52 / ARCH_V3 §2.3):

    Repository / tag / full reference / ID / digest / size / architecture / OS /
    layers / containers using the image (read-only relation) / environment (collapsed by default).

    Two convergences from phase 3 (§2.3):
    - Layers show the first 5 by default and can be expanded (there can be dozens; showing all
      would make the page very long)
    - Multiple tags are rendered as chips instead of one row each

    The content is clearly taller than the window, so the page still scrolls (Kirigami.Page does not scroll);
    the body is width-capped and centered so lines stay short in wide windows (§1.2).
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kcmutils as KCM
import org.kde.kirigami as Kirigami
import org.kde.kcm.docker as Kontainer

import "components" as Components

KCM.SimpleKCM {
    id: page

    property string imageId: ""

    readonly property var controller: kcm.controller.imageDetail
    /*! Write-operation controller (ARCH_V4 §2.4). */
    readonly property var operations: kcm.controller.operations
    readonly property bool ready: controller.loadStateKey === "ready"

    /*! Layer count shown while collapsed (§2.3). */
    readonly property int collapsedLayerCount: 5
    property bool layersExpanded: false

    /*! Request a return to the list page (main.qml connects it to StackView.pop).
        Not named backRequested: Kirigami.Page already declares a signal by that name. */
    signal closeRequested
    /*! Open the registry auth page with this image's registry prefilled (ARCH_V5_V8 §2.7). */
    signal registryAuthRequested(string serverAddress)
    /*! Create a container from this image (phase 7 §4.5). */
    signal createContainerRequested(string imageReference)
    /*! Open a related container's detail page (same interaction as member rows on network detail). */
    signal containerRequested(string containerId)

    /*!
        Delete semantics (ARCH_V4 §2.4):
        - Tagged → delete by repository:tag, removing that tag only and keeping the others
        - Multiple tags → additionally offer "Delete all tags", which uses force=true
        - Untagged (dangling) → delete by ID
        When a container references the image the engine answers 409; the message explains why and
        nothing is force-deleted automatically.
    */
    readonly property bool targetBusy: {
        // As above: the call itself creates no dependency, so read stateRevision first
        page.operations.stateRevision;
        return page.operations.isImageBusy(page.imageId);
    }
    readonly property bool canRemove: page.ready && !page.targetBusy && page.operations.writeAllowed
    readonly property bool hasTags: controller.primaryTag.length > 0
    readonly property bool multipleTags: controller.tags.count > 1

    /*! Content max width: ~42 gridUnit, keeping lines short in wide windows (§1.2). */
    readonly property real contentMaxWidth: Kirigami.Units.gridUnit * 42

    Component.onCompleted: {
        // Re-entering the same image must reload too
        if (page.imageId.length > 0) {
            controller.imageId = page.imageId;
            controller.start();
        }
        // Layer list collapses by default: expose only the first N rows from the model (§2.3)
        controller.layers.limit = page.collapsedLayerCount;
    }

    Component.onDestruction: controller.stop()

    function toggleLayers() {
        page.layersExpanded = !page.layersExpanded;
        // 0 = no limit
        controller.layers.limit = page.layersExpanded ? 0 : page.collapsedLayerCount;
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        /* Results of operations started on this page (pull / delete) appear here */
        Components.OperationMessage {
            Layout.fillWidth: true
            operations: page.operations
        }

        /* ------------------------------------------------------------------ */
        /* Header: back + image name (same as container detail, so users always know which image this is) */
        /* ------------------------------------------------------------------ */
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            QQC2.Button {
                text: i18n("Images")
                icon.name: "go-previous"
                onClicked: page.closeRequested()
            }
            Kirigami.Heading {
                Layout.fillWidth: true
                level: 2
                elide: Text.ElideMiddle
                text: controller.primaryTag.length > 0 ? controller.primaryTag : i18n("Image")
            }
            Components.CopyButton {
                value: controller.primaryTag
                fieldLabel: i18n("image reference")
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: controller.loadStateKey === "loading"
            spacing: Kirigami.Units.smallSpacing

            QQC2.BusyIndicator {
                running: parent.visible
                implicitHeight: Kirigami.Units.iconSizes.smallMedium
                implicitWidth: implicitHeight
            }
            QQC2.Label {
                text: i18n("Loading image details…")
                opacity: 0.7
            }
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: controller.loadStateKey === "error"
            type: Kirigami.MessageType.Error
            text: controller.errorText.length > 0 ? controller.errorText : i18n("Unable to retrieve image details.")
            actions: [
                Kirigami.Action {
                    text: i18n("Create container…")
                    icon.name: "list-add"
                    enabled: page.operations.writeAllowed
                    onTriggered: page.createContainerRequested(page.controller.primaryTag)
                },
                Kirigami.Action {
                    text: i18n("Registry logins…")
                    icon.name: "dialog-password"
                    onTriggered: page.registryAuthRequested(kcm.controller.registryAuth.serverAddressForImage(page.imageId))
                },
                Kirigami.Action {
                    text: i18n("Retry")
                    icon.name: "view-refresh"
                    onTriggered: controller.refresh()
                },
                Kirigami.Action {
                    text: i18n("Back")
                    icon.name: "go-previous"
                    onTriggered: page.closeRequested()
                }
            ]
        }

        /* ------------------------------------------------------------------ */
        /* Body: width-capped and centered (§1.2)                                */
        /* ------------------------------------------------------------------ */
        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: page.contentMaxWidth
            visible: page.ready
            spacing: Kirigami.Units.largeSpacing

            /* ---------------- Overview ---------------- */
            Kirigami.FormLayout {
                /*
                 * Shrink to content and align left: Kirigami's FormLayout right-aligns the
                 * `[label][field]` group, so at full width the block drifts to the right
                 * (user report: too far right). Fields needing width (long commands) set their
                 * own Layout.preferredWidth instead of relying on the row.
                 */
                Layout.fillWidth: false
                Layout.alignment: Qt.AlignLeft

                Components.CopyableText {
                    Kirigami.FormData.label: i18n("Repository:")
                    value: controller.primaryRepository
                    placeholderText: i18n("<none> (dangling)")
                    fieldLabel: i18n("repository")
                }

                Components.CopyableText {
                    Kirigami.FormData.label: i18n("Tag:")
                    value: controller.tagName
                    placeholderText: i18n("<none>")
                    fieldLabel: i18n("tag")
                }

                Components.CopyableText {
                    Kirigami.FormData.label: i18n("Full reference:")
                    value: controller.primaryTag
                    placeholderText: i18n("<none>")
                    fieldLabel: i18n("image reference")
                }

                Components.CopyableText {
                    Kirigami.FormData.label: i18n("Image ID:")
                    value: controller.shortId
                    copyValue: controller.imageId
                    fieldLabel: i18n("image ID")
                }

                QQC2.Label {
                    Kirigami.FormData.label: i18n("Created:")
                    visible: Kontainer.Format.isValid(controller.created)
                    text: i18nc("@info absolute time and relative", "%1 (%2 ago)", Kontainer.Format.absoluteTime(controller.created), Kontainer.Format.elapsed(controller.created))
                }
                QQC2.Label {
                    Kirigami.FormData.label: i18n("Size:")
                    text: Kontainer.Format.byteSize(controller.sizeBytes)
                }
                QQC2.Label {
                    Kirigami.FormData.label: i18n("Architecture:")
                    visible: text.length > 0
                    text: controller.variant.length > 0 ? controller.architecture + "/" + controller.variant : controller.architecture
                }
                QQC2.Label {
                    Kirigami.FormData.label: i18n("OS:")
                    visible: text.length > 0
                    text: controller.os
                }
                QQC2.Label {
                    Kirigami.FormData.label: i18n("Author:")
                    visible: text.length > 0
                    text: controller.author
                }
            }

            /* ---------------- Tags (chips, §2.3) ---------------- */
            Kirigami.Heading {
                level: 3
                visible: controller.tags.count > 0
                text: i18n("Tags")
            }
            Kirigami.Separator {
                Layout.fillWidth: true
                visible: controller.tags.count > 0
            }

            Flow {
                Layout.fillWidth: true
                visible: controller.tags.count > 0
                spacing: Kirigami.Units.smallSpacing

                Repeater {
                    model: controller.tags

                    delegate: Kirigami.Badge {
                        required property string label

                        text: label
                        // Tags are display-only, not clickable to delete (phase 3 has no write actions)
                        Accessible.role: Accessible.StaticText
                        Accessible.name: label
                    }
                }
            }

            /* ---------------- Digests ---------------- */
            Kirigami.Heading {
                level: 3
                visible: controller.digests.count > 0
                text: i18n("Digests")
            }
            Kirigami.Separator {
                Layout.fillWidth: true
                visible: controller.digests.count > 0
            }

            ColumnLayout {
                Layout.fillWidth: true
                visible: controller.digests.count > 0
                spacing: 0

                Repeater {
                    model: controller.digests

                    delegate: ColumnLayout {
                        required property string label
                        required property string value

                        Layout.fillWidth: true
                        spacing: 0

                        QQC2.Label {
                            Layout.fillWidth: true
                            text: label
                            font.family: "monospace"
                            elide: Text.ElideMiddle
                        }
                        QQC2.Label {
                            Layout.fillWidth: true
                            text: value
                            // Never assign `font` as a whole and then font.family: QML errors with
                            // "Property has already been assigned"
                            font.family: "monospace"
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                            opacity: 0.6
                            elide: Text.ElideMiddle
                        }
                    }
                }
            }

            /* ---------------- Layers (first 5 while collapsed, §2.3) ---------------- */
            Kirigami.Heading {
                level: 3
                text: i18ncp("@info image layer count", "Layers (%1)", "Layers (%1)", controller.layerCount)
            }
            Kirigami.Separator {
                Layout.fillWidth: true
            }

            QQC2.Label {
                Layout.fillWidth: true
                text: i18n("The Docker API reports layer digests, not per-layer sizes.")
                font: Kirigami.Theme.smallFont
                opacity: 0.6
                wrapMode: Text.WordWrap
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                Repeater {
                    model: controller.layers

                    delegate: RowLayout {
                        required property string label
                        required property string value

                        objectName: "layerEntry"
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        QQC2.Label {
                            text: label
                            Layout.preferredWidth: Kirigami.Units.gridUnit * 2
                            opacity: 0.7
                        }
                        QQC2.Label {
                            text: value
                            font.family: "monospace"
                            Layout.fillWidth: true
                            elide: Text.ElideMiddle
                        }
                    }
                }
            }

            QQC2.Button {
                Layout.alignment: Qt.AlignHCenter
                visible: controller.layerCount > page.collapsedLayerCount
                text: page.layersExpanded ? i18n("Show fewer layers") : i18ncp("@info show all image layers", "Show all %1 layers", "Show all %1 layers", controller.layerCount)
                icon.name: page.layersExpanded ? "arrow-up" : "arrow-down"
                onClicked: page.toggleLayers()
            }

            /* ---------------- Containers using this image (read-only relation, §52) ---------------- */
            Kirigami.Heading {
                level: 3
                text: i18n("Containers")
            }
            Kirigami.Separator {
                Layout.fillWidth: true
            }

            Components.EmptyPlaceholder {
                Layout.fillWidth: true
                message: controller.usedByContainers.empty ? i18n("No containers use this image.") : ""
            }

            ColumnLayout {
                Layout.fillWidth: true
                visible: !controller.usedByContainers.empty
                spacing: 0

                Repeater {
                    model: controller.usedByContainers

                    // Same row as "network → connected containers": state icon + name + state + arrow,
                    // and it **is clickable** to open the container (user report: this used to be inert)
                    delegate: QQC2.ItemDelegate {
                        id: usedByRow

                        required property string label
                        required property string value
                        required property string stateKey
                        required property string target

                        objectName: "usedByEntry"
                        Layout.fillWidth: true
                        enabled: usedByRow.target.length > 0
                        onClicked: page.containerRequested(usedByRow.target)

                        contentItem: RowLayout {
                            spacing: Kirigami.Units.smallSpacing

                            Kirigami.Icon {
                                objectName: "usedByStateIcon"
                                visible: usedByRow.stateKey.length > 0
                                source: Kontainer.Presentation.stateIconName(usedByRow.stateKey)
                                color: Components.StatusPalette.color(Kontainer.Presentation.stateSemanticKey(usedByRow.stateKey, "none"))
                                implicitWidth: Kirigami.Units.iconSizes.small
                                implicitHeight: Kirigami.Units.iconSizes.small
                            }
                            QQC2.Label {
                                Layout.fillWidth: true
                                text: usedByRow.label
                                elide: Text.ElideRight
                            }
                            QQC2.Label {
                                text: usedByRow.value
                                font: Kirigami.Theme.smallFont
                                opacity: 0.8
                            }
                            Kirigami.Icon {
                                source: "go-next-symbolic"
                                implicitWidth: Kirigami.Units.iconSizes.small
                                implicitHeight: Kirigami.Units.iconSizes.small
                                opacity: 0.6
                            }
                        }
                    }
                }
            }

            /* ---------------- Environment (collapsed by default, §40) ---------------- */
            Components.CollapsibleSection {
                Layout.fillWidth: true
                contentObjectName: "imageEnvironmentValues"
                title: i18ncp("@info environment variable count", "Environment (%1 variable)", "Environment (%1 variables)", controller.environmentCount)

                // An image's environment is a string list ("KEY=value"), not a key/value model
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    /* As in MainPage: use the length, not the QStringList property itself, so list
                       changes do not rebuild items during layout polish. */
                    Repeater {
                        model: controller.environment.length

                        delegate: QQC2.Label {
                            required property int index

                            Layout.fillWidth: true
                            text: controller.environment[index]
                            font.family: "monospace"
                            elide: Text.ElideMiddle
                        }
                    }
                }
            }
        }
    }

    /*!
        Delete entry point (ARCH_V4 §2.4): destructive actions live on the detail page only and always
        require confirmation. When the permission gate denies writes the whole footer is absent.
    */
    footer: QQC2.ToolBar {
        id: actionBar

        objectName: "imageActionBar"
        visible: page.operations.writeAllowed
        position: QQC2.ToolBar.Footer

        contentItem: RowLayout {
            spacing: Kirigami.Units.smallSpacing

            QQC2.BusyIndicator {
                objectName: "imageBusyIndicator"
                visible: page.targetBusy
                running: page.targetBusy
                implicitWidth: Kirigami.Units.iconSizes.smallMedium
                implicitHeight: Kirigami.Units.iconSizes.smallMedium
            }

            Item {
                Layout.fillWidth: true
            }

            QQC2.Button {
                objectName: "imageRemoveAllTagsButton"
                visible: page.canRemove && page.multipleTags
                text: i18n("Delete all tags…")
                icon.name: "edit-delete"
                onClicked: removeAllTagsDialog.open()
            }

            QQC2.Button {
                objectName: "imageRemoveButton"
                visible: page.canRemove
                text: i18n("Delete")
                icon.name: "edit-delete"
                onClicked: removeDialog.open()
            }
        }
    }

    /* Delete a single tag (the default action) */
    Components.ConfirmDialog {
        id: removeDialog

        objectName: "removeImageDialog"
        headingText: i18n("Delete image tag")
        questionText: page.hasTags
            ? i18n("Delete the tag “%1”?", controller.primaryTag)
            : i18n("Delete the image “%1”?", controller.shortId)
        consequenceText: i18n("Only this tag is removed. Layers shared with other images are kept.")
        acceptText: i18n("Delete")
        destructive: true
        onConfirmed: {
            if (page.hasTags) {
                page.operations.removeImage(controller.primaryTag, false);
            } else {
                page.operations.removeImage(page.imageId, false);
            }
        }
    }

    /* Delete all tags: force, with no extra options such as "clean up afterwards" */
    Components.ConfirmDialog {
        id: removeAllTagsDialog

        objectName: "removeAllImageTagsDialog"
        headingText: i18n("Delete all tags")
        questionText: i18n("Delete every tag of this image (%1 tags)?", controller.tags.count)
        consequenceText: i18n("All tags of this image are removed. Containers that reference it can no longer be started.")
        acceptText: i18n("Delete all tags")
        destructive: true
        onConfirmed: page.operations.removeImage(page.imageId, true)
    }

    /* After a successful delete this page's target no longer exists: return to the list */
    Connections {
        target: page.operations
        function onImageRemoved(id) {
            if (id === page.imageId || id === controller.primaryTag) {
                page.closeRequested();
            }
        }
    }
}
