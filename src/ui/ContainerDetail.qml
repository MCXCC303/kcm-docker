/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Container Detail (ARCH_V2 §7 / ARCH_V3 §2.2):

    Runtime information reorganized the way users think, not a prettified docker inspect JSON dump.

    Sections (§2.2): overview / resources / network / mounts / logs.
    - Overview: top-level facts (Name/State/Health/Status/Image/ID/times) + Runtime + Configuration
            + Environment/Labels (collapsed by default, §40)
    - Resources: CPU / memory / network / block IO + short-term trends
    - Network: interfaces + ports
    - Mounts: bind / volume
    - Logs: implemented in phase 4; says so explicitly instead of leaving a blank page

    The page itself does not scroll: each section scrolls on its own (§2.2 constraint 4), hence
    KCM.AbstractKCM rather than SimpleKCM.

    ARCH_V3 §2.1: no state-semantics decisions here; semantic keys and icon names come from C++.
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kcmutils as KCM
import org.kde.kirigami as Kirigami
import org.kde.kcm.docker as Kontainer

import "components" as Components

KCM.AbstractKCM {
    id: page

    property string containerId: ""

    readonly property var controller: kcm.controller.containerDetail
    /*! Write-operation controller (ARCH_V4 §2.3). */
    readonly property var operations: kcm.controller.operations
    readonly property bool ready: controller.loadStateKey === "ready"
    /*! Health outranks state: an Unhealthy Running container must look wrong (§11.3) */
    readonly property string stateSemanticKey: Kontainer.Presentation.stateSemanticKey(controller.stateKey, controller.healthKey)
    readonly property bool healthVisible: controller.healthKey !== "unknown" && controller.healthKey !== "none"
    /*! Content max width: ~42 gridUnit, keeping lines short in wide windows (§1.2). */
    readonly property real contentMaxWidth: Kirigami.Units.gridUnit * 42

    /*!
        Write-action visibility (ARCH_V4 §2.3):
        - Without write permission the whole footer disappears (not silently disabled)
        - Reversible actions (start / stop / restart) run directly; delete needs confirmation
        - Running containers get no delete button, with the reason stated: letting the engine
          answer 409 and explaining afterwards is worse
    */
    /*! Host node title: the daemon's Name is the host name, falling back to the local host name. */
    readonly property string engineHostName: kcm.controller.engine.engineName.length > 0 ? kcm.controller.engine.engineName : i18n("Host")

    readonly property bool targetBusy: {
        // As above: function calls create no dependency, so read stateRevision first
        page.operations.stateRevision;
        return page.operations.isContainerBusy(page.containerId);
    }
    readonly property bool canStart: page.ready && !page.targetBusy && page.operations.writeAllowed
        && (controller.stateKey === "exited" || controller.stateKey === "created" || controller.stateKey === "dead")
    readonly property bool canStop: page.ready && !page.targetBusy && page.operations.writeAllowed
        && (controller.stateKey === "running" || controller.stateKey === "paused" || controller.stateKey === "restarting")
    /*! Pause only makes sense for **running** containers; paused ones get Resume (user report ①). */
    readonly property bool canPause: page.ready && !page.targetBusy && page.operations.writeAllowed
        && controller.stateKey === "running"
    readonly property bool canUnpause: page.ready && !page.targetBusy && page.operations.writeAllowed
        && controller.stateKey === "paused"
    readonly property bool canRestart: page.ready && !page.targetBusy && page.operations.writeAllowed
        && (controller.stateKey === "running" || controller.stateKey === "paused")
    readonly property bool canRemove: page.ready && !page.targetBusy && page.operations.writeAllowed
        && controller.stateKey !== "running" && controller.stateKey !== "paused" && controller.stateKey !== "restarting"

    /*! Field label for address rows in a network entry: IPv4 / IPv6 / gateway. */
    function networkValueLabel(entryKey: string): string {
        if (entryKey === "network-ipv6") {
            return i18n("IPv6:");
        }
        if (entryKey === "network-gateway") {
            return i18n("Gateway:");
        }
        return i18n("IPv4:");
    }

    /*! Request a return to the list page (main.qml connects it to StackView.pop).
        Not named backRequested: Kirigami.Page already declares a signal by that name. */
    signal closeRequested
    /*! Clone this container's configuration (phase 7 §4.5): config only, no runtime state. */
    signal cloneRequested(string containerId)
    /*! Open the detail page of the image this container uses. */
    signal imageRequested(string imageId)

    /*! "Save as preset" result message (inline feedback in the mount row). */
    property string mountPresetMessage: ""
    property bool mountPresetMessageVisible: false

    /*! Network name awaiting disconnect confirmation (used by the dialog). */
    property string pendingNetworkName: ""
    /*! Whether the connect-network inline panel is open (plus the selected network id / aliases). */
    property bool connectPanelOpen: false
    property string connectNetworkId: ""
    property string connectAliases: ""

    /*! Whether a network can still be connected (an attached one obviously cannot). */
    /*
     * Connectable networks (a property, not a function).
     *
     * User report: after disconnecting, the network still showed as "already connected" with a
     * greyed button, because this was a function call and bindings do not track data changes (the
     * fifth time this trap bit the project). As a `readonly property` binding block it re-evaluates
     * whenever `connectedNetworkNames` or the network model changes.
     */
    readonly property var connectedNetworkNameList: page.controller.connectedNetworkNames

    /*!
     * Networks that can still be connected (list and count are properties the UI reads directly).
     *
     * `model.count` is read explicitly alongside `model.summaries()` on purpose: a C++
     * Q_INVOKABLE call creates no dependency, and only a NOTIFYable property makes this binding
     * re-evaluate when the network list changes (the same trap, hit many times).
     */
    readonly property var connectableNetworks: {
        const model = kcm.controller.networkModel;
        const connected = page.connectedNetworkNameList;
        const result = [];
        const rowCount = model ? model.count : 0; // ← establishes the dependency, do not remove
        if (!model || rowCount === 0) {
            return result;
        }
        for (const summary of model.summaries()) {
            if (connected.indexOf(summary.name) < 0) {
                result.push(summary);
            }
        }
        return result;
    }
    readonly property int connectableNetworkCount: page.connectableNetworks.length

    function isConnectable(networkName: string): bool {
        return page.connectedNetworkNameList.indexOf(networkName) < 0;
    }

    /*! Submit the inline panel's connection (the controller supplies user-facing failure text). */
    function submitConnectNetwork(): void {
        if (page.connectNetworkId.length === 0) {
            return;
        }
        if (page.operations.connectContainerToNetwork(page.connectNetworkId, page.containerId, page.connectAliases)) {
            page.connectPanelOpen = false;
            page.connectNetworkId = "";
            page.connectAliases = "";
        }
    }

    /*!
     * Network summaries (`{id, name, driver}`): the data source for the connect dialog.
     *
     * Each evaluation reads `networkModel.count`, so the dialog follows model refreshes
     * (QML does not track function calls; an explicit property read creates the dependency).
     */
    /*! Number of connectable networks (0 = none, which the panel explains). */


    Component.onCompleted: {
        // start() must be unconditional: even with the same container id (A → back → A again) the
        // page must re-inspect and restart low-frequency checks and stats sampling (§27/§46).
        if (page.containerId.length > 0) {
            controller.containerId = page.containerId;
            controller.start();
        }
    }

    // Leaving the page: stop stats sampling and release metric history (§27)
    Components.ConfirmDialog {
        id: disconnectNetworkDialog

        objectName: "disconnectNetworkDialog"
        headingText: i18n("Disconnect from network")
        questionText: i18n("Disconnect this container from “%1”?", page.pendingNetworkName)
        // Disconnecting a network in use interrupts traffic, so this consequence must be spelled out
        consequenceText: i18n("The container loses this network's addresses and aliases; connections through it may be interrupted.")
        acceptText: i18n("Disconnect")
        destructive: true
        onConfirmed: {
            // Name → id conversion has exactly one implementation (NetworkModel::idForName)
            const networkId = kcm.controller.networkModel.idForName(page.pendingNetworkName);
            page.operations.disconnectContainerFromNetwork(networkId.length > 0 ? networkId : page.pendingNetworkName,
                                                           page.containerId);
        }
    }

    Component.onDestruction: controller.stop()

    /*!
        Write-action footer (reserved by ARCH_V3 §2.2 constraint 5, filled in ARCH_V4 §2.3).

        Reversible and destructive actions share one row, but delete sits at the far right with its
        own confirmation; while busy the buttons are disabled with a progress indicator so a double
        click cannot fire a second request.
    */
    footer: QQC2.ToolBar {
        id: actionBar

        objectName: "containerActionBar"
        visible: page.operations.writeAllowed
        position: QQC2.ToolBar.Footer

        contentItem: RowLayout {
            spacing: Kirigami.Units.smallSpacing

            QQC2.Button {
                objectName: "detailStartButton"
                visible: page.canStart
                text: i18n("Start")
                icon.name: "media-playback-start"
                onClicked: page.operations.startContainer(page.containerId)
            }

            QQC2.Button {
                objectName: "detailStopButton"
                visible: page.canStop
                text: i18n("Stop")
                icon.name: "media-playback-stop"
                onClicked: page.operations.stopContainer(page.containerId)
            }

            QQC2.Button {
                objectName: "detailPauseButton"
                visible: page.canPause
                text: i18n("Pause")
                icon.name: "media-playback-pause"
                onClicked: page.operations.pauseContainer(page.containerId)
            }

            QQC2.Button {
                objectName: "detailUnpauseButton"
                visible: page.canUnpause
                text: i18n("Resume")
                icon.name: "media-playback-start"
                onClicked: page.operations.unpauseContainer(page.containerId)
            }

            QQC2.Button {
                objectName: "detailRestartButton"
                visible: page.canRestart
                text: i18n("Restart")
                icon.name: "view-refresh"
                onClicked: page.operations.restartContainer(page.containerId)
            }

            QQC2.BusyIndicator {
                objectName: "detailBusyIndicator"
                visible: page.targetBusy
                running: page.targetBusy
                implicitWidth: Kirigami.Units.iconSizes.smallMedium
                implicitHeight: Kirigami.Units.iconSizes.smallMedium
            }

            // No delete while running: saying why beats letting the engine answer 409
            QQC2.Label {
                objectName: "removeBlockedHint"
                visible: page.ready && !page.canRemove && !page.targetBusy && page.operations.writeAllowed
                    && (controller.stateKey === "running" || controller.stateKey === "paused" || controller.stateKey === "restarting")
                text: i18n("Stop the container to delete it.")
                font: Kirigami.Theme.smallFont
                opacity: 0.7
            }

            Item {
                Layout.fillWidth: true
            }

            QQC2.Button {
                objectName: "detailRemoveButton"
                visible: page.canRemove
                text: i18n("Delete")
                icon.name: "edit-delete"
                onClicked: removeDialog.open()
            }
        }
    }


    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        /* Operation results from this page (start / stop / restart / delete) are shown here */
        Components.OperationMessage {
            Layout.fillWidth: true
            operations: page.operations
        }

        /* ------------------------------------------------------------------ */
        /* Header: back + name + copy name                                        */
        /* ------------------------------------------------------------------ */
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.smallSpacing

            QQC2.Button {
                text: i18n("Containers")
                icon.name: "go-previous"
                onClicked: page.closeRequested()
            }
            Kirigami.Heading {
                Layout.fillWidth: true
                level: 2
                elide: Text.ElideRight
                text: page.controller.name.length > 0 ? page.controller.name : i18n("Container")
            }
            Components.CopyButton {
                value: page.controller.name
                fieldLabel: i18n("container name")
            }
        }

        /* ------------------------------------------------------------------ */
        /* Loading / error (§31: a detail failure never breaks the list page)     */
        /* ------------------------------------------------------------------ */
        RowLayout {
            Layout.fillWidth: true
            visible: page.controller.loadStateKey === "loading"
            spacing: Kirigami.Units.smallSpacing

            QQC2.BusyIndicator {
                running: parent.visible
                implicitHeight: Kirigami.Units.iconSizes.smallMedium
                implicitWidth: implicitHeight
            }
            QQC2.Label {
                text: i18n("Loading container details…")
                opacity: 0.7
            }
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: page.controller.loadStateKey === "error"
            type: Kirigami.MessageType.Error
            text: page.controller.errorText.length > 0 ? page.controller.errorText : i18n("Unable to retrieve container details.")
            actions: [
                Kirigami.Action {
                    text: i18n("Retry")
                    icon.name: "view-refresh"
                    onTriggered: page.controller.refresh()
                },
                Kirigami.Action {
                    text: i18n("Back")
                    icon.name: "go-previous"
                    onTriggered: page.closeRequested()
                }
            ]
        }

        /* ------------------------------------------------------------------ */
        /* Section switcher (§1.3: room for logs and later actions)               */
        /* ------------------------------------------------------------------ */
        QQC2.TabBar {
            id: sectionBar

            objectName: "detailTabBar"
            Layout.fillWidth: true
            visible: page.ready

            // Logs are a long-lived stream: connected when the section is entered, dropped on
            // leaving (§3.1.4). Index 4 = logs
            onCurrentIndexChanged: {
                if (currentIndex === 4) {
                    page.controller.startLogs();
                } else {
                    page.controller.stopLogs();
                }
                // The network section needs the network list (connect panel, name → id): refresh it on
                // entry, or users who never opened the Networks tab see "no connectable networks"
                if (currentIndex === 2) {
                    kcm.controller.refreshNetworks();
                }
            }

            QQC2.TabButton {
                text: i18nc("@title:tab container overview", "Overview")
            }
            QQC2.TabButton {
                text: i18nc("@title:tab container resources", "Resources")
            }
            QQC2.TabButton {
                text: i18nc("@title:tab container network", "Network")
            }
            QQC2.TabButton {
                text: i18nc("@title:tab container mounts", "Mounts")
            }
            QQC2.TabButton {
                text: i18nc("@title:tab container logs", "Logs")
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
            visible: page.ready
        }

        StackLayout {
            id: sectionStack

            objectName: "detailSectionStack"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: page.ready
            // Switching sections never touches the controller lifecycle: no re-inspect, no restart of
            // stats sampling (§2.2 constraints 2/3)
            currentIndex: sectionBar.currentIndex

            /* ============================ Overview ============================ */
            QQC2.ScrollView {
                id: overviewScroll

                clip: true
                contentWidth: availableWidth

                ColumnLayout {
                    width: Math.min(overviewScroll.availableWidth, page.contentMaxWidth)
                    x: Math.max(0, (overviewScroll.availableWidth - width) / 2)
                    spacing: Kirigami.Units.largeSpacing

                    /* ---------------- Top-level facts ---------------- */
                    Kirigami.FormLayout {
                        /*
                         * Shrink to content and align left: Kirigami's FormLayout right-aligns the
                         * `[label][field]` group, so at full width the block drifts to the right
                         * (user report: too far right). Fields needing width (long commands) set their
                         * own Layout.preferredWidth instead of relying on the row.
                         */
                        Layout.fillWidth: false
                        Layout.alignment: Qt.AlignLeft

                        RowLayout {
                            Kirigami.FormData.label: i18n("State:")

                            Components.StatusChip {
                                semanticKey: page.stateSemanticKey
                                iconName: Kontainer.Presentation.stateIconName(page.controller.stateKey)
                                text: page.controller.stateText
                            }
                            QQC2.Label {
                                visible: page.healthVisible
                                text: page.controller.healthText
                                font: Kirigami.Theme.smallFont
                                opacity: 0.8
                            }
                        }

                        QQC2.Label {
                            Kirigami.FormData.label: i18n("Status:")
                            text: page.controller.status.length > 0 ? page.controller.status : i18n("Unknown")
                        }

                        QQC2.ItemDelegate {
                            id: imageRow

                            Kirigami.FormData.label: i18n("Image:")
                            Layout.fillWidth: true
                            /*
                             * The image row opens image detail — the same interaction as network
                             * members and related containers (arrow + clickable row). Not clickable
                             * when the image ID is empty (the engine did not supply one).
                             */
                            enabled: page.controller.imageId.length > 0
                            onClicked: page.imageRequested(page.controller.imageId)

                            contentItem: RowLayout {
                                spacing: Kirigami.Units.smallSpacing

                                QQC2.Label {
                                    objectName: "detailImageLabel"
                                    Layout.fillWidth: true
                                    text: page.controller.image
                                    elide: Text.ElideMiddle
                                }
                                Kirigami.Icon {
                                    source: "go-next-symbolic"
                                    implicitWidth: Kirigami.Units.iconSizes.small
                                    implicitHeight: Kirigami.Units.iconSizes.small
                                    opacity: 0.6
                                }
                            }
                        }

                        Components.CopyableText {
                            Kirigami.FormData.label: i18n("Container ID:")
                            value: page.controller.shortId
                            copyValue: page.controller.containerId
                            fieldLabel: i18n("container ID")
                        }

                        QQC2.Label {
                            Kirigami.FormData.label: i18n("Created:")
                            visible: Kontainer.Format.isValid(page.controller.created)
                            text: Kontainer.Format.absoluteTime(page.controller.created)
                        }

                        QQC2.Label {
                            Kirigami.FormData.label: i18n("Started:")
                            visible: Kontainer.Format.isValid(page.controller.started)
                            text: i18nc("@info absolute time and relative", "%1 (%2 ago)", Kontainer.Format.absoluteTime(page.controller.started), Kontainer.Format.elapsed(page.controller.started))
                        }

                        QQC2.Label {
                            Kirigami.FormData.label: i18n("Finished:")
                            visible: Kontainer.Format.isValid(page.controller.finished)
                            text: Kontainer.Format.absoluteTime(page.controller.finished)
                        }
                    }

                    /* ---------------- Runtime ---------------- */
                    Kirigami.Heading {
                        level: 3
                        text: i18n("Runtime")
                    }
                    Kirigami.Separator {
                        Layout.fillWidth: true
                    }

                    Kirigami.FormLayout {
                        /*
                         * Shrink to content and align left: Kirigami's FormLayout right-aligns the
                         * `[label][field]` group, so at full width the block drifts to the right
                         * (user report: too far right). Fields needing width (long commands) set their
                         * own Layout.preferredWidth instead of relying on the row.
                         */
                        Layout.fillWidth: false
                        Layout.alignment: Qt.AlignLeft

                        QQC2.Label {
                            Kirigami.FormData.label: i18n("Restart count:")
                            text: String(page.controller.restartCount)
                        }
                        QQC2.Label {
                            Kirigami.FormData.label: i18n("Exit code:")
                            text: String(page.controller.exitCode)
                        }
                        QQC2.Label {
                            Kirigami.FormData.label: i18n("OOM killed:")
                            visible: page.controller.oomKilled
                            text: i18n("Yes")
                            color: Components.StatusPalette.color("negative")
                        }
                        QQC2.Label {
                            Kirigami.FormData.label: i18n("Process ID:")
                            visible: page.controller.pid > 0
                            text: String(page.controller.pid)
                        }
                        QQC2.Label {
                            Kirigami.FormData.label: i18n("Restart policy:")
                            visible: text.length > 0
                            text: page.controller.restartPolicy
                        }
                        QQC2.Label {
                            Kirigami.FormData.label: i18n("Platform:")
                            visible: text.length > 0
                            text: page.controller.platform
                        }
                    }

                    /* ---------------- Configuration ---------------- */
                    Kirigami.Heading {
                        level: 3
                        text: i18n("Configuration")
                    }
                    Kirigami.Separator {
                        Layout.fillWidth: true
                    }

                    Kirigami.FormLayout {
                        /*
                         * Shrink to content and align left: Kirigami's FormLayout right-aligns the
                         * `[label][field]` group, so at full width the block drifts to the right
                         * (user report: too far right). Fields needing width (long commands) set their
                         * own Layout.preferredWidth instead of relying on the row.
                         */
                        Layout.fillWidth: false
                        Layout.alignment: Qt.AlignLeft

                        /*
                         * Entrypoint and command: one-click copyable (user report A2).
                         *
                         * Width policy — the form shrinks to content, so these rows must set their own:
                         *   - no width: the implicit content width pushes the field out of the panel;
                         *   - `implicitWidth: 0`: the field collapses to a few dozen pixels (measured in
                         *     a render) and the ellipsis swallows everything.
                         * A fixed 26 gridUnit (≈470px) keeps short commands on one line, wraps long ones
                         * inside the field, and neither overflows nor inflates the page; the full value
                         * stays reachable through the copy button and tooltip.
                         */
                        Item {
                            Kirigami.FormData.label: i18n("Entrypoint:")
                            Layout.preferredWidth: Kirigami.Units.gridUnit * 26
                            implicitHeight: entrypointRow.implicitHeight

                            Components.CopyableText {
                                id: entrypointRow

                                objectName: "detailEntrypointRow"
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                value: page.controller.entrypoint.join(" ")
                                fieldLabel: i18n("entry point")
                                wrap: true
                            }
                        }
                        Item {
                            Kirigami.FormData.label: i18n("Command:")
                            Layout.preferredWidth: Kirigami.Units.gridUnit * 26
                            implicitHeight: commandRow.implicitHeight

                            Components.CopyableText {
                                id: commandRow

                                objectName: "detailCommandRow"
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                value: page.controller.command.join(" ")
                                fieldLabel: i18n("command")
                                wrap: true
                            }
                        }
                        QQC2.Label {
                            Kirigami.FormData.label: i18n("Working directory:")
                            visible: text.length > 0
                            text: page.controller.workingDirectory
                            font.family: "monospace"
                        }
                        QQC2.Label {
                            Kirigami.FormData.label: i18n("User:")
                            visible: text.length > 0
                            text: page.controller.user
                        }
                        QQC2.Label {
                            Kirigami.FormData.label: i18n("Hostname:")
                            visible: text.length > 0
                            text: page.controller.hostname
                        }
                    }

                    /* ---------------- Environment / labels (collapsed by default) ---------------- */
                    Components.CollapsibleSection {
                        Layout.fillWidth: true
                        contentObjectName: "environmentValues"
                        title: i18ncp("@info environment variable count", "Environment (%1 variable)", "Environment (%1 variables)", page.controller.environmentCount)

                        Components.KeyValueList {
                            model: page.controller.environmentVariables
                        }
                    }

                    Components.CollapsibleSection {
                        Layout.fillWidth: true
                        contentObjectName: "labelValues"
                        title: i18ncp("@info label count", "Labels (%1)", "Labels (%1)", page.controller.labels.count)

                        Components.KeyValueList {
                            model: page.controller.labels
                        }
                    }
                }
            }

            /* ============================ Resources ============================ */
            QQC2.ScrollView {
                id: resourcesScroll

                clip: true
                contentWidth: availableWidth

                ResourceView {
                    width: Math.min(resourcesScroll.availableWidth, page.contentMaxWidth)
                    x: Math.max(0, (resourcesScroll.availableWidth - width) / 2)
                    metrics: page.controller.metrics
                }
            }

            /* ============================ Network ============================ */
            QQC2.ScrollView {
                id: networkScroll

                clip: true
                contentWidth: availableWidth

                ColumnLayout {
                    width: Math.min(networkScroll.availableWidth, page.contentMaxWidth)
                    x: Math.max(0, (networkScroll.availableWidth - width) / 2)
                    spacing: Kirigami.Units.smallSpacing

                    Components.EmptyPlaceholder {
                        Layout.fillWidth: true
                        message: page.controller.networks.empty ? i18n("No network information.") : ""
                    }

                    // Connect/disconnect network (ARCH_V5_V8 §3.4): no entry point in read-only mode
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        QQC2.Button {
                            objectName: "connectNetworkEntryButton"
                            visible: page.operations.writeAllowed
                            text: page.connectPanelOpen ? i18n("Cancel") : i18n("Connect to a network…")
                            icon.name: "network-connect"
                            onClicked: {
                                page.connectPanelOpen = !page.connectPanelOpen;
                                page.connectNetworkId = "";
                                page.connectAliases = "";
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                        }
                    }

                    /* Connect network: an **inline panel**, not a popup.
                       A Kirigami.Dialog keeps a second instance of its content outside the window, and
                       model-driven children may not be created at all while offscreen or not yet shown
                       (measured: the dialog was open and empty). Connecting is a pick-one-here action,
                       so inline is more direct. */
                    ColumnLayout {
                        objectName: "connectNetworkPanel"
                        Layout.fillWidth: true
                        visible: page.connectPanelOpen && page.operations.writeAllowed
                        spacing: Kirigami.Units.smallSpacing

                        Kirigami.InlineMessage {
                            objectName: "connectNetworkError"
                            Layout.fillWidth: true
                            visible: page.operations.resultKey === "error" && page.operations.resultText.length > 0
                            type: Kirigami.MessageType.Error
                            text: page.operations.resultText
                        }

                        Kirigami.InlineMessage {
                            objectName: "connectNetworkEmptyMessage"
                            Layout.fillWidth: true
                            visible: page.connectableNetworkCount === 0
                            type: Kirigami.MessageType.Information
                            text: i18n("This container is already connected to every network. Create another network first.")
                        }

                        QQC2.Label {
                            Layout.fillWidth: true
                            visible: page.connectableNetworkCount > 0
                            text: i18n("Choose a network:")
                            font.bold: true
                        }

                        Repeater {
                            // Use the model (a property) directly: the list follows model changes,
                            // not a function snapshot
                            model: kcm.controller.networkModel

                            delegate: QQC2.RadioButton {
                                id: networkOption

                                required property string name
                                required property string id
                                required property string driver
                                required property bool predefined

                                objectName: "connectNetworkOption"
                                Layout.fillWidth: true
                                // Attached networks are labelled but not selectable again
                                // (no pointless re-connect)
                                enabled: page.isConnectable(networkOption.name)
                                text: networkOption.name + " · " + networkOption.driver
                                    + (networkOption.enabled ? "" : " — " + i18n("already connected"))
                                onClicked: page.connectNetworkId = networkOption.id
                            }
                        }

                        QQC2.TextField {
                            objectName: "connectNetworkAliasesField"
                            Layout.fillWidth: true
                            visible: page.connectableNetworkCount > 0
                            placeholderText: i18n("Aliases (optional, comma separated)")
                            Accessible.name: i18n("Aliases")
                            onTextChanged: page.connectAliases = text
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            QQC2.Button {
                                objectName: "connectNetworkButton"
                                text: i18n("Connect")
                                icon.name: "network-connect"
                                enabled: page.connectNetworkId.length > 0
                                onClicked: page.submitConnectNetwork()
                            }

                            QQC2.Label {
                                Layout.fillWidth: true
                                text: i18n("Other containers on the same network can reach this one by its aliases.")
                                font: Kirigami.Theme.smallFont
                                opacity: 0.75
                                elide: Text.ElideRight
                            }
                        }
                    }

                    /*  Network entries **avoid FormLayout**: one GridLayout per entry inside a
                        Repeater matched the core-dump shape "outer layout → item box → inner
                        GridLayout sizeHint → FormData attached-property lookup". Plain row layouts,
                        as used by the mounts section, remove that nesting. */
                    Repeater {
                        model: page.controller.networks

                        delegate: ColumnLayout {
                            required property string label
                            required property string value
                            required property string detail
                            required property string entryKey

                            objectName: "networkEntry"
                            Layout.fillWidth: true
                            spacing: 0

                            /* The network name appears on the main entry only: in IPv6/gateway entries
                               `label` is already a heading like "IPv6", so repeating it would give
                               "IPv6: IPv6" */
                            RowLayout {
                                Layout.fillWidth: true
                                visible: entryKey === "network"
                                spacing: Kirigami.Units.smallSpacing

                                QQC2.Label {
                                    text: i18n("Network:")
                                    Layout.preferredWidth: Kirigami.Units.gridUnit * 6
                                    opacity: 0.75
                                    elide: Text.ElideRight
                                }
                                QQC2.Label {
                                    text: label
                                    font.family: "monospace"
                                    Layout.fillWidth: true
                                    elide: Text.ElideMiddle
                                }
                                // Disconnect may interrupt traffic, so it goes through a confirmation dialog
                                QQC2.Button {
                                    objectName: "disconnectNetworkButton"
                                    visible: page.operations.writeAllowed
                                    flat: true
                                    text: i18n("Disconnect")
                                    icon.name: "network-disconnect"
                                    onClicked: {
                                        page.pendingNetworkName = label;
                                        disconnectNetworkDialog.open();
                                    }
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                visible: value.length > 0
                                spacing: Kirigami.Units.smallSpacing

                                QQC2.Label {
                                    text: page.networkValueLabel(entryKey)
                                    Layout.preferredWidth: Kirigami.Units.gridUnit * 6
                                    opacity: 0.75
                                    elide: Text.ElideRight
                                }
                                QQC2.Label {
                                    text: value
                                    font.family: "monospace"
                                    Layout.fillWidth: true
                                    elide: Text.ElideMiddle
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                visible: entryKey === "network" && detail.length > 0
                                spacing: Kirigami.Units.smallSpacing

                                QQC2.Label {
                                    text: i18n("MAC:")
                                    Layout.preferredWidth: Kirigami.Units.gridUnit * 6
                                    opacity: 0.75
                                }
                                QQC2.Label {
                                    text: detail
                                    font.family: "monospace"
                                    Layout.fillWidth: true
                                    elide: Text.ElideMiddle
                                }
                            }
                        }
                    }

                    Kirigami.Heading {
                        level: 3
                        text: i18n("Port mapping")
                    }
                    Kirigami.Separator {
                        Layout.fillWidth: true
                    }

                    Components.EmptyPlaceholder {
                        Layout.fillWidth: true
                        objectName: "portsEmptyPlaceholder"
                        message: page.controller.publishedPorts.empty && page.controller.unpublishedPorts.empty ? i18n("No published ports.") : ""
                    }

                    /* Topology: container ports left, host bindings right, lines decorative (§2.1.2) */
                    Components.PortTopology {
                        Layout.fillWidth: true
                        // Use the **grouped** model: several bindings of one container port
                        // merge into one branch line
                        visible: !page.controller.portGroups.empty
                        model: page.controller.portGroups
                        containerLabel: page.controller.name.length > 0 ? page.controller.name : i18n("Container")
                        hostLabel: page.engineHostName
                        // One container always keeps one color: the seed is the container id (ARCH_V4 §2.1.2)
                        colorSeed: page.controller.containerId
                    }

                    /* EXPOSEd-only ports with no host mapping: no host endpoint, so no line is drawn */
                    QQC2.Label {
                        Layout.fillWidth: true
                        Layout.topMargin: Kirigami.Units.smallSpacing
                        visible: !page.controller.unpublishedPorts.empty
                        text: i18n("Exposed but not published")
                        font.bold: true
                    }

                    Flow {
                        Layout.fillWidth: true
                        visible: !page.controller.unpublishedPorts.empty
                        spacing: Kirigami.Units.smallSpacing

                        Repeater {
                            model: page.controller.unpublishedPorts

                            delegate: Components.FieldChip {
                                required property string containerChipText
                                required property string protocol

                                objectName: "unpublishedPortChip"
                                muted: true
                                text: containerChipText
                                Accessible.name: i18n("%1, exposed but not published to the host", containerChipText)
                            }
                        }
                    }
                }
            }

            /* ============================ Mounts ============================ */
            QQC2.ScrollView {
                id: mountsScroll

                clip: true
                contentWidth: availableWidth

                ColumnLayout {
                    width: Math.min(mountsScroll.availableWidth, page.contentMaxWidth)
                    x: Math.max(0, (mountsScroll.availableWidth - width) / 2)
                    spacing: Kirigami.Units.smallSpacing

                    Components.EmptyPlaceholder {
                        Layout.fillWidth: true
                        objectName: "mountsEmptyPlaceholder"
                        message: page.controller.mounts.empty ? i18n("No mounts.") : ""
                    }

                    // "Save as preset" result: inline feedback, no need to look elsewhere
                    Kirigami.InlineMessage {
                        objectName: "mountPresetMessage"
                        Layout.fillWidth: true
                        visible: page.mountPresetMessageVisible
                        type: Kirigami.MessageType.Positive
                        text: page.mountPresetMessage
                        showCloseButton: true
                        onVisibleChanged: {
                            if (!visible) {
                                page.mountPresetMessageVisible = false;
                            }
                        }
                    }

                    /* Hint when opening the host folder fails (path missing / no file manager) */
                    Kirigami.InlineMessage {
                        objectName: "mountActionMessage"
                        Layout.fillWidth: true
                        visible: page.controller.mountActionError.length > 0
                        type: Kirigami.MessageType.Warning
                        text: page.controller.mountActionError
                        showCloseButton: true
                        onVisibleChanged: {
                            if (!visible) {
                                page.controller.dismissMountActionError();
                            }
                        }
                    }

                    Repeater {
                        model: page.controller.mounts

                        delegate: ColumnLayout {
                            id: mountRow

                            required property int index
                            required property string typeKey
                            required property string source
                            required property string destination
                            required property string mode
                            required property string volumeName
                            required property string sourceStateKey
                            required property bool openable

                            objectName: "mountEntry"
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing / 2

                            /* First row: type + read/write mode + missing-host-path warning */
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Kirigami.Units.smallSpacing

                                Components.FieldChip {
                                    objectName: "mountTypeChip"
                                    text: mountRow.typeKey.length > 0 ? mountRow.typeKey : i18n("mount")
                                }

                                Components.FieldChip {
                                    objectName: "mountModeChip"
                                    text: mountRow.mode
                                    muted: mountRow.mode === "ro"
                                }

                                // Named volumes show their name; otherwise only an opaque host path shows
                                Components.FieldChip {
                                    objectName: "mountVolumeChip"
                                    visible: mountRow.volumeName.length > 0
                                    text: mountRow.volumeName
                                }

                                Item {
                                    Layout.fillWidth: true
                                }

                                Components.StatusChip {
                                    objectName: "mountSourceWarning"
                                    visible: mountRow.sourceStateKey === "missing" || mountRow.sourceStateKey === "notADirectory"
                                    semanticKey: "neutral"
                                    iconName: "dialog-warning"
                                    text: mountRow.sourceStateKey === "notADirectory" ? i18n("Not a directory") : i18n("Host path missing")
                                }
                            }

                            /* Second row: host path → container path
                               (the container path ends flush right; both elide in the middle so a long
                               one cannot squeeze the other out) */
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Kirigami.Units.smallSpacing
                                visible: mountRow.source.length > 0

                                QQC2.Label {
                                    objectName: "mountSourceLabel"
                                    // Mount sources are potentially sensitive (§40): detail page only,
                                    // never logged
                                    text: mountRow.source
                                    font.family: "monospace"
                                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                                    // Host paths are usually longer: take the remaining width and elide in
                                    // the middle (head and tail identify it better than a plain tail cut)
                                    elide: Text.ElideMiddle
                                    horizontalAlignment: Text.AlignLeft
                                    Layout.fillWidth: true
                                }

                                Kirigami.Icon {
                                    source: "go-next-symbolic"
                                    Accessible.ignored: true
                                    implicitWidth: Kirigami.Units.iconSizes.small
                                    implicitHeight: Kirigami.Units.iconSizes.small
                                    opacity: 0.6
                                }

                                QQC2.Label {
                                    objectName: "mountDestinationLabel"
                                    text: mountRow.destination
                                    font.family: "monospace"
                                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                                    elide: Text.ElideMiddle
                                    // Right-aligned: container paths form a fixed-length column meant to be
                                    // scanned at a glance, so it hugs the right edge instead of drifting
                                    // with the host path's length
                                    horizontalAlignment: Text.AlignRight
                                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                                    // A very long container path takes at most 40% of the width;
                                    // the rest stays with the host path
                                    Layout.maximumWidth: Math.max(Kirigami.Units.gridUnit * 6, mountRow.width * 0.4)
                                }
                            }

                            /* Third row: actions (tmpfs and missing paths offer no open action) */
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Kirigami.Units.smallSpacing

                                QQC2.Button {
                                    objectName: "mountOpenButton"
                                    visible: mountRow.openable
                                    text: i18n("Open host folder")
                                    icon.name: "folder-open"
                                    onClicked: page.controller.openMountHostPath(mountRow.index)
                                }

                                Components.CopyButton {
                                    visible: mountRow.source.length > 0
                                    value: mountRow.source
                                    fieldLabel: i18n("host path")
                                }

                                Components.CopyButton {
                                    value: mountRow.destination
                                    fieldLabel: i18n("container path")
                                }

                                // Save this mount as a preset (phase 7 §4.2) for one-click reuse
                                // when creating a container
                                QQC2.Button {
                                    objectName: "mountSavePresetButton"
                                    visible: mountRow.source.length > 0
                                    text: i18n("Save as preset")
                                    icon.name: "bookmark-new"
                                    onClicked: {
                                        const id = kcm.controller.mountPresets.add(mountRow.source, mountRow.destination,
                                                                                   mountRow.typeKey === "volume" ? "volume" : "bind",
                                                                                   mountRow.mode === "ro", "");
                                        page.mountPresetMessage = id.length > 0
                                            ? i18n("Saved as a preset.")
                                            : i18n("This mount is already saved as a preset.");
                                        page.mountPresetMessageVisible = true;
                                    }
                                }
                            }
                        }
                    }
                }
            }

            /* ============================ Logs (§3.1) ============================ */
            ColumnLayout {
                Layout.margins: Kirigami.Units.largeSpacing

                Components.LogConsole {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    logs: page.controller.logs
                }
            }
        }
    }

    /* Delete confirmation: wording and consequence text are fixed (ARCH_V4 §2.2.5) */
    Components.ConfirmDialog {
        id: removeDialog

        objectName: "removeContainerDialog"
        headingText: i18n("Delete container")
        questionText: i18n("Delete the container “%1”?", controller.name)
        consequenceText: i18n("The container is removed. Its anonymous and named volumes are kept.")
        acceptText: i18n("Delete")
        destructive: true
        onConfirmed: page.operations.removeContainer(page.containerId)
    }

    /* After a successful delete this page's target is gone: return to the list (already refreshed) */
    Connections {
        target: page.operations
        function onContainerRemoved(id) {
            if (id === page.containerId) {
                page.closeRequested();
            }
        }
    }
}
