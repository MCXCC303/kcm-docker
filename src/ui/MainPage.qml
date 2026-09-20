/*
    SPDX-FileCopyrightText: 2026 kcm-docker developers
    SPDX-License-Identifier: GPL-2.0-or-later

    Kontainer home page (ARCH_V2 §5 / ARCH_V3 §2): engine status + overview + storage + container/image lists.

    main.qml pushes it into a StackView as the root page; cards emit signals and main.qml navigates.

    ARCH_V3 §2.1: this file makes no state-semantics decisions — semantic keys
    (positive/neutral/negative) and icon names all come from C++ (StatusController / Presentation);
    only layout and wording live here.
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.kcm.docker as Kontainer

import "components" as Components

Kirigami.Page {
    id: root

    Component.onCompleted: {
        if (root.startTab > 0 && tabBar) {
            tabBar.currentIndex = root.startTab;
            if (root.startTab === 4) {
                root.controller.refreshPorts();
            }
        }
    }

    readonly property var controller: kcm.controller
    readonly property var containerList: controller.containerList
    readonly property var imageList: controller.imageList
    readonly property var networkList: controller.networkList
    readonly property var volumeList: controller.volumeList

    /*! Card activated: main.qml wires it to navigation (ARCH_V2 §43: navigation is a KCM-layer concern) */
    signal containerActivated(string containerId)
    /*! The ports page asks to open a container detail page (containers are its "jump" targets). */
    signal portContainerActivated(string containerId)
    /*! Open the runtime configuration page (daemon.json); scope = user | system. */
    signal configureRuntimeRequested(string scope)
    /*! Open the registry auth page (ARCH_V5_V8 §2.7): used by the Images toolbar and failure hints. */
    signal registryAuthRequested(string serverAddress)
    signal imageActivated(string imageId)
    /*! Open network detail (phase 6 §3.2); main.qml handles navigation. */
    signal networkActivated(string networkId)
    /*! Whether the inline build-image form is open (phase 8 §5.4). */
    property bool buildPanelOpen: false

    /*! Open volume detail (phase 6 §3.5). */
    signal volumeActivated(string volumeName)
    /*! Open the create-container wizard (phase 7 §4.4); an empty presetImage starts from scratch. */
    signal createContainerRequested(string presetImage)

    /*! Inline panels on the volumes tab (create / prune). */
    property bool volumeCreatePanelOpen: false
    property bool volumePrunePanelOpen: false

    /*! Reclaimable-space text: counts **known** sizes only and says how many volumes have unknown size. */
    function pruneReclaimableText(): string {
        const model = root.controller.volumeModel;
        const known = model.knownUnusedSize();
        const unknown = model.unknownUnusedSizeCount();
        let text = Kontainer.Format.byteSize(known);
        if (unknown > 0) {
            text += " + " + i18ncp("@info volumes with unknown size",
                                   "one volume of unknown size",
                                   "%1 volumes of unknown size",
                                   unknown);
        }
        return text;
    }

    /*! Overview tiles (view-layer aggregation only; an empty semanticKey means no state semantics) */
    readonly property var tiles: [
        {
            label: i18n("Containers"),
            value: controller.engine.containerTotal,
            icon: "application-x-executable",
            semanticKey: ""
        },
        {
            label: i18n("Running"),
            value: controller.engine.containersRunning,
            icon: "media-playback-start",
            semanticKey: "positive"
        },
        {
            label: i18n("Paused"),
            value: controller.engine.containersPaused,
            icon: "media-playback-pause",
            semanticKey: "neutral"
        },
        {
            label: i18n("Stopped"),
            value: controller.engine.containersStopped,
            icon: "media-playback-stop",
            semanticKey: "negative"
        },
        {
            label: i18n("Images"),
            value: controller.engine.imageCount,
            icon: "image-x-generic",
            semanticKey: ""
        }
    ]

    readonly property bool countsReady: controller.engine.countsAvailable

    /*! Write-operation controller (ARCH_V4 §2.2.4): card buttons and result messages read from it. */
    readonly property var operations: root.controller.operations

    /*
     * Connection text (B1): engine data alone is not enough — with docker.service stopped the socket
     * still exists, so the old implementation showed "Connected" while every action failed. Hence
     * controller.connectionKey, which also weighs service state and the last refresh result.
     */
    function connectionText(key: string): string {
        switch (key) {
        case "connected":
            return i18n("Connected");
        case "connectedServicesDown":
            return i18n("Connected, but a service is not running");
        case "disconnectedServicesDown":
            return i18n("Not connected: a service is not running");
        default:
            return i18n("Not connected");
        }
    }

    /*! Whether the connection state deserves a warning color (service down, or not connected). */
    function connectionNeedsAttention(key: string): bool {
        return key !== "connected";
    }

    function engineStateText(stateKey: string): string {
        switch (stateKey) {
        case "loading":
            return i18n("Loading Docker status…");
        case "ready":
        case "refreshing":
        case "partial":
            return i18n("Connected");
        default:
            return i18n("Not connected");
        }
    }

    /* ------------------------------------------------------------------ */
    /* Helpers for the ports page (M3)                                    */
    /* ------------------------------------------------------------------ */
    /*!
     * Debug start tab (0 = containers).
     *
     * Injected by the host (the `kcmshell6` side reads `KCM_DOCKER_START_TAB`) purely so a given page
     * opens for screenshot review or debugging; default behavior is unchanged.
     */
    property int startTab: 0

    /*! Ports page view mode: `list` (default) or `map` (range map). */
    property string portViewMode: "list"

    /*! "Declared but not published" row count (computed in the controller; QML never scans the model). */
    readonly property int portsDeclaredCount: root.controller.declaredNotPublishedCount

    function openContainerFromPorts(containerId, containerName): void {
        if (containerId.length === 0) {
            return;
        }
        root.portContainerActivated(containerId);
    }

    /* ------------------------------------------------------------------ */
    /* Empty states (§33: four cases must stay distinct; logic here, rendering in the component) */
    /* ------------------------------------------------------------------ */

    readonly property var containersEmptyState: {
        if (root.containerList.count > 0 || root.controller.containersStateKey === "error") {
            return {
                message: "",
                actionText: ""
            };
        }
        if (root.controller.containers.count === 0) {
            return {
                message: i18n("No containers found."),
                actionText: ""
            };
        }
        if (root.containerList.searchText.length > 0) {
            return {
                message: i18nc("@info no container matches the search", "No containers match “%1”.", root.containerList.searchText),
                actionText: i18n("Clear search")
            };
        }
        return {
            message: i18n("No containers match the current filter."),
            actionText: i18n("Show all containers")
        };
    }

    readonly property var imagesEmptyState: {
        if (root.imageList.count > 0 || root.controller.imagesStateKey === "error") {
            return {
                message: "",
                actionText: ""
            };
        }
        if (root.controller.images.count === 0) {
            return {
                message: i18n("No images found."),
                // Offer a genuinely useful action when writes are allowed
                // (ARCH_V3_pre §1.3: empty states may carry an action button)
                actionText: root.operations.writeAllowed ? i18n("Pull an image") : ""
            };
        }
        if (root.imageList.searchText.length > 0) {
            return {
                message: i18nc("@info no image matches the search", "No images match “%1”.", root.imageList.searchText),
                actionText: i18n("Clear search")
            };
        }
        return {
            message: i18n("No images match the current filter."),
            actionText: i18n("Show all images")
        };
    }

    /*!
        Clear the current tab's search and filter.

        Once the user has typed, the declarative TextField.text binding is broken by internal
        assignment (QQC2 behavior, not a bug here), so the field and the combo box must both be
        cleared explicitly.
    */
    /*!
        Empty-state guidance action: pull an image when the image list is empty, otherwise clear filters.
    */
    function handleEmptyAction() {
        if (tabBar.currentIndex === 1 && root.controller.images.count === 0 && root.operations.writeAllowed) {
            pullDialog.reset();
            pullDialog.open();
            return;
        }
        root.clearCurrentTabFilters();
    }

    function clearCurrentTabFilters() {
        if (tabBar.currentIndex === 0) {
            root.containerList.searchText = "";
            root.containerList.stateFilter = "all";
        } else {
            root.imageList.searchText = "";
            root.imageList.useFilter = "all";
        }
        searchField.text = "";
        filterBox.currentIndex = 0;
    }

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        /* ------------------------------------------------------------------ */
        /* Header: connection state + Last Updated + failure/stale (§15/§16/§34) */
        /* ------------------------------------------------------------------ */
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.smallSpacing

            Components.StatusChip {
                semanticKey: root.controller.engineStateSemanticKey
                iconName: root.controller.engineStateIconName
                text: root.controller.engineStateKey === "loading"
                    ? root.engineStateText(root.controller.engineStateKey)
                    : root.connectionText(root.controller.connectionKey)
            }
            QQC2.Label {
                text: "•"
                opacity: 0.5
            }
            QQC2.Label {
                text: Kontainer.Format.isValid(root.controller.lastUpdated)
                    ? i18nc("@info last successful update", "Updated %1 ago", Kontainer.Format.elapsed(root.controller.lastUpdated))
                    : i18n("Not updated yet")
                opacity: 0.75
                font: Kirigami.Theme.smallFont
            }
            QQC2.Label {
                visible: root.controller.updateFailed
                text: i18n("Update failed")
                color: Components.StatusPalette.color("negative")
                font: Kirigami.Theme.smallFont
            }
            QQC2.Label {
                visible: root.controller.stale
                text: i18n("Data is stale")
                color: Components.StatusPalette.color("neutral")
                font: Kirigami.Theme.smallFont
            }
            QQC2.Label {
                // Without this hint, switching auto-refresh off makes users think the UI froze
                visible: !root.controller.autoRefreshEnabled
                text: i18n("Auto-refresh is off")
                color: Components.StatusPalette.color("neutral")
                font: Kirigami.Theme.smallFont
            }

            Item {
                Layout.fillWidth: true
            }

            QQC2.BusyIndicator {
                // Background refreshes show only a light indicator and keep existing content (§34)
                running: root.controller.busy
                visible: running
                implicitHeight: Kirigami.Units.iconSizes.small
                implicitWidth: implicitHeight
            }
            QQC2.Label {
                text: root.controller.endpoint
                opacity: 0.6
                font: Kirigami.Theme.smallFont
                elide: Text.ElideLeft
                Layout.maximumWidth: Kirigami.Units.gridUnit * 14
            }
        }

        /* ------------------------------------------------------------------ */
        /* Whole-page error (reached only when every high-frequency dataset failed, §30) */
        /* ------------------------------------------------------------------ */
        /* ------------------------------------------------------------------ */
        /* Write-permission gate (ARCH_V4 §2.2.3): read-only hides all write entries; this says why */
        /* ------------------------------------------------------------------ */
        Kirigami.InlineMessage {
            objectName: "writeAccessBanner"
            Layout.fillWidth: true
            visible: !root.operations.writeAllowed && root.operations.writeAccessText.length > 0
            type: Kirigami.MessageType.Information
            text: root.operations.writeAccessText
        }

        /* One of the places operation results (success / failure / cancel) are shown */
        Components.OperationMessage {
            Layout.fillWidth: true
            operations: root.operations
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: root.controller.stateKey === "error"
            type: Kirigami.MessageType.Error
            text: root.controller.engineError.length > 0 ? root.controller.engineError : i18n("Unable to connect to Docker Engine.")
            actions: [
                Kirigami.Action {
                    text: i18n("Retry")
                    icon.name: "view-refresh"
                    onTriggered: root.controller.refresh()
                }
            ]
        }

        /* ------------------------------------------------------------------ */
        /* Overview (tiles + storage) */
        /*                                                                     */
        /* In a short window the overview must not squeeze the lists away: cap its height and let it  */
        /* scroll internally, so the lists keep a minimum height (§19: narrow / small KCM windows).   */
        /* ------------------------------------------------------------------ */
        QQC2.ScrollView {
            id: overviewScroll

            objectName: "overviewScroll"

            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(overviewColumn.implicitHeight, root.height * 0.45)
            Layout.maximumHeight: root.height * 0.45
            clip: true

            ColumnLayout {
                id: overviewColumn

                width: overviewScroll.availableWidth
                spacing: Kirigami.Units.smallSpacing

                /* Tiles reflow with window width (§1.2): 5 columns wide / 3 medium / 2 narrow.
                   Breakpoints are expressed in gridUnit, never raw pixels. */
                GridLayout {
                    id: tileGrid

                    objectName: "tileGrid"

                    Layout.fillWidth: true
                    columnSpacing: Kirigami.Units.smallSpacing
                    rowSpacing: Kirigami.Units.smallSpacing

                    columns: {
                        if (tileGrid.width >= Kirigami.Units.gridUnit * 40) {
                            return 5;
                        }
                        if (tileGrid.width >= Kirigami.Units.gridUnit * 26) {
                            return 3;
                        }
                        return 2;
                    }

                    /*  model must be a **stable number**: root.tiles is a JS array re-evaluated on every
                        data change, and using it directly makes Repeater destroy and rebuild all tiles
                        on each refresh. That "items destroyed while the layout is computing sizes"
                        pattern makes Qt's layout engine touch a destructed item during polish
                        (segfaults under the kcmshell6 QQuickWidget host), hence indexing here. */
                    Repeater {
                        model: root.tiles.length

                        delegate: Components.StatTile {
                            required property int index

                            Layout.fillWidth: true
                            label: root.tiles[index].label
                            value: root.countsReady ? String(root.tiles[index].value) : i18n("—")
                            iconName: root.tiles[index].icon
                            semanticKey: root.tiles[index].semanticKey
                            tintWhenNonZero: true
                            tooltip: root.countsReady ? "" : i18n("Engine summary is unavailable")
                        }
                    }
                }

                

                StorageView {
                    Layout.fillWidth: true
                    controller: root.controller
                    // Volume usage invites a follow-up: which ones? Jump to the volumes tab (index 3)
                    onSegmentActivated: function (entryKey) {
                        if (entryKey === "volumes") {
                            tabBar.currentIndex = 3;
                        }
                    }
                }
            }
        }

        /* ------------------------------------------------------------------ */
        /* Tabs                                                               */
        /* ------------------------------------------------------------------ */
        QQC2.TabBar {
            id: tabBar
            objectName: "tabBar"
            Layout.fillWidth: true

            // Networks and volumes are low-frequency: refreshed on tab entry only, never on the 5 s poll.
            // Indexes: 0 containers / 1 images / 2 networks / 3 volumes / 4 ports / 5 presets / 6 engine
            onCurrentIndexChanged: {
                if (tabBar.currentIndex === 2) {
                    root.controller.refreshNetworks();
                } else if (tabBar.currentIndex === 3) {
                    root.controller.refreshVolumes();
                } else if (tabBar.currentIndex === 4) {
                    // Ports page: refresh containers and inspect each running one (for its "declared" ports)
                    root.controller.refreshPorts();
                }
            }

            QQC2.TabButton {
                text: i18ncp("@title:tab container list", "Containers (%1)", "Containers (%1)", root.controller.containers.count)
            }
            QQC2.TabButton {
                text: i18ncp("@title:tab image list", "Images (%1)", "Images (%1)", root.controller.images.count)
            }
            QQC2.TabButton {
                text: i18ncp("@title:tab network list", "Networks (%1)", "Networks (%1)", root.controller.networkModel.count)
            }
            QQC2.TabButton {
                text: i18ncp("@title:tab volume list", "Volumes (%1)", "Volumes (%1)", root.controller.volumeModel.count)
            }
            QQC2.TabButton {
                // User requirement: show the **running** port count (not all declared ones),
                // unaffected by the filter
                text: i18ncp("@title:tab host port list", "Ports (%1)", "Ports (%1)", root.controller.inUsePortCount)
            }
            QQC2.TabButton {
                text: i18nc("@title:tab mount presets", "Mount presets")
            }
            QQC2.TabButton {
                text: i18nc("@title:tab engine information", "Engine")
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        /* ------------------------------------------------------------------ */
        /* Toolbar: search / filter / sort (§9/§10)                           */
        /* ------------------------------------------------------------------ */
        RowLayout {
            Layout.fillWidth: true
            // Shared search/filter row for containers and images: other tabs have their own (see tabBar)
            visible: tabBar.currentIndex === 0 || tabBar.currentIndex === 1
            spacing: Kirigami.Units.smallSpacing

            Kirigami.SearchField {
                id: searchField

                Layout.fillWidth: true
                placeholderText: tabBar.currentIndex === 0 ? i18n("Search containers (name, image, ID)…") : i18n("Search images (repository, tag, ID)…")
                // Conditions live in the proxy, so background refreshes do not reset them (§32)
                text: tabBar.currentIndex === 0 ? root.containerList.searchText : root.imageList.searchText
                onTextEdited: {
                    if (tabBar.currentIndex === 0) {
                        root.containerList.searchText = text;
                    } else {
                        root.imageList.searchText = text;
                    }
                }
            }

            QQC2.ComboBox {
                id: filterBox

                readonly property var containerFilters: [
                    {
                        key: "all",
                        label: i18n("All states")
                    },
                    {
                        key: "running",
                        label: i18n("Running")
                    },
                    {
                        key: "paused",
                        label: i18n("Paused")
                    },
                    {
                        key: "stopped",
                        label: i18n("Stopped")
                    },
                    {
                        key: "restarting",
                        label: i18n("Restarting")
                    },
                    {
                        key: "dead",
                        label: i18n("Dead")
                    }
                ]
                readonly property var imageFilters: [
                    {
                        key: "all",
                        label: i18n("All images")
                    },
                    {
                        key: "in-use",
                        label: i18n("In use")
                    },
                    {
                        key: "dangling",
                        label: i18n("Dangling")
                    }
                ]

                model: tabBar.currentIndex === 0 ? containerFilters : imageFilters
                textRole: "label"
                valueRole: "key"
                onActivated: {
                    if (tabBar.currentIndex === 0) {
                        root.containerList.stateFilter = currentValue;
                    } else {
                        root.imageList.useFilter = currentValue;
                    }
                }
                Component.onCompleted: currentIndex = indexOfValue(tabBar.currentIndex === 0 ? root.containerList.stateFilter : root.imageList.useFilter)
                onModelChanged: currentIndex = indexOfValue(tabBar.currentIndex === 0 ? root.containerList.stateFilter : root.imageList.useFilter)
            }

            QQC2.ComboBox {
                id: sortBox

                readonly property var containerSorts: [
                    {
                        key: "name",
                        label: i18n("Sort: Name")
                    },
                    {
                        key: "state",
                        label: i18n("Sort: State")
                    },
                    {
                        key: "created",
                        label: i18n("Sort: Created")
                    }
                ]
                readonly property var imageSorts: [
                    {
                        key: "repository",
                        label: i18n("Sort: Repository")
                    },
                    {
                        key: "created",
                        label: i18n("Sort: Created")
                    },
                    {
                        key: "size",
                        label: i18n("Sort: Size")
                    }
                ]

                model: tabBar.currentIndex === 0 ? containerSorts : imageSorts
                textRole: "label"
                valueRole: "key"
                onActivated: {
                    if (tabBar.currentIndex === 0) {
                        root.containerList.sortKey = currentValue;
                    } else {
                        root.imageList.sortKey = currentValue;
                    }
                }
                Component.onCompleted: currentIndex = indexOfValue(tabBar.currentIndex === 0 ? root.containerList.sortKey : root.imageList.sortKey)
                onModelChanged: currentIndex = indexOfValue(tabBar.currentIndex === 0 ? root.containerList.sortKey : root.imageList.sortKey)
            }

            // Build image (phase 8 §5.4): no entry point in read-only mode
            QQC2.Button {
                objectName: "buildImageEntryButton"
                visible: tabBar.currentIndex === 1 && root.operations.writeAllowed
                text: i18n("Build image…")
                icon.name: "run-build"
                onClicked: root.buildPanelOpen = !root.buildPanelOpen
            }

            // Create container (phase 7 §4.4): no entry point in read-only mode
            QQC2.Button {
                objectName: "createContainerEntryButton"
                visible: tabBar.currentIndex === 0 && root.operations.writeAllowed
                text: i18n("Create container…")
                icon.name: "list-add"
                onClicked: {
                    // Creating a container needs the network list, which refreshes only on tab entry,
                    // so fetch it here — otherwise no network is selectable until that tab is opened
                    root.controller.refreshNetworks();
                    root.createContainerRequested("");
                }
            }

            // Registry login (ARCH_V5_V8 §2.7): log in before pulling from a private registry
            QQC2.Button {
                objectName: "registryAuthEntryButton"
                visible: tabBar.currentIndex === 1
                text: i18n("Registry logins…")
                icon.name: "dialog-password"
                onClicked: root.registryAuthRequested("")
            }

            // Pull image (ARCH_V4 §2.4): the only entry point that adds images.
            // Without write permission the button disappears entirely instead of greying out silently.
            QQC2.Button {
                objectName: "pullImageEntryButton"
                visible: tabBar.currentIndex === 1 && root.operations.writeAllowed
                text: root.operations.activePullCount > 0
                    ? i18n("Pull image (%1 running)", root.operations.activePullCount)
                    : i18n("Pull image")
                icon.name: "download"
                onClicked: {
                    pullDialog.reset();
                    pullDialog.open();
                }
            }
        }

        /* ------------------------------------------------------------------ */
        /* Content area                                                       */
        /* ------------------------------------------------------------------ */
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: Kirigami.Units.gridUnit * 8
            currentIndex: tabBar.currentIndex

            /* ---------------------------- Containers ---------------------------- */
            ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    visible: root.controller.containersStateKey === "error"
                    type: Kirigami.MessageType.Error
                    text: i18n("Unable to retrieve the container list: %1", root.controller.containersError)
                }

                Components.EmptyPlaceholder {
                    objectName: "containersEmptyPlaceholder"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    message: root.containersEmptyState.message
                    actionText: root.containersEmptyState.actionText
                    actionIconName: "edit-clear"
                    onActionTriggered: root.handleEmptyAction()
                }

                ListView {
                    id: containerView
                    objectName: "containerView"

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.containersEmptyState.message.length === 0
                    clip: true
                    // Keep old data and the scroll position during background refreshes (§32/§34)
                    property real savedContentY: 0
                    model: root.containerList
                    spacing: Kirigami.Units.smallSpacing / 2
                    // Keyboard navigation (§38)
                    keyNavigationEnabled: true
                    activeFocusOnTab: true

                    QQC2.ScrollBar.vertical: QQC2.ScrollBar {}

                    /*
                     * Fallback: restore the scroll position when the model is **really** reset.
                     *
                     * Not the main mechanism — the model no longer resets on value changes (see
                     * model/keyed_list_model.h, which was the root cause of "refresh jumps back to the
                     * top"), and restoring contentY right after a reset clamps to the new content
                     * height, which is 0 until the new delegates are laid out, so the fallback cannot
                     * rescue it either. It stays only so that a reset from some other source is not
                     * completely unprotected.
                     */
                    Connections {
                        target: containerView.model

                        function onModelAboutToBeReset() {
                            containerView.savedContentY = containerView.contentY;
                        }
                        function onModelReset() {
                            containerView.contentY = containerView.savedContentY;
                        }
                    }

                    delegate: ContainerCard {
                        operations: root.operations
                        onActivated: root.containerActivated(containerId)
                    }
                }
            }

            /* ---------------------------- Images ---------------------------- */
            ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    visible: root.controller.imagesStateKey === "error"
                    type: Kirigami.MessageType.Error
                    text: i18n("Unable to retrieve the image list: %1", root.controller.imagesError)
                }

                /* Pull progress: concurrent, continues in the background, failures keep their reason
                   (ARCH_V4 §2.4) */
                Components.BuildImagePanel {
                    id: buildPanel

                    Layout.fillWidth: true
                    operations: root.operations
                    formOpen: root.buildPanelOpen
                    buildCacheBytes: root.controller.storage.buildCacheBytes > 0 ? root.controller.storage.buildCacheBytes : 0
                    visible: root.buildPanelOpen || root.operations.builds.count > 0
                    onImageRequested: function (imageId) {
                        root.imageActivated(imageId);
                    }
                }

                Components.PullProgressList {
                    Layout.fillWidth: true
                    operations: root.operations
                    // 401/403 failures go straight to that registry's login dialog
                    // instead of leaving the user to find the entry point
                    onLoginRequested: function (reference) {
                        root.registryAuthRequested(root.controller.registryAuth.serverAddressForImage(reference));
                    }
                }

                Components.EmptyPlaceholder {
                    objectName: "imagesEmptyPlaceholder"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    message: root.imagesEmptyState.message
                    actionText: root.imagesEmptyState.actionText
                    actionIconName: "edit-clear"
                    onActionTriggered: root.handleEmptyAction()
                }

                ListView {
                    id: imageView
                    objectName: "imageView"

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.imagesEmptyState.message.length === 0
                    clip: true
                    property real savedContentY: 0
                    model: root.imageList
                    spacing: Kirigami.Units.smallSpacing / 2
                    keyNavigationEnabled: true
                    activeFocusOnTab: true

                    QQC2.ScrollBar.vertical: QQC2.ScrollBar {}

                    Connections {
                        target: imageView.model

                        function onModelAboutToBeReset() {
                            imageView.savedContentY = imageView.contentY;
                        }
                        function onModelReset() {
                            imageView.contentY = imageView.savedContentY;
                        }
                    }

                    delegate: ImageCard {
                        onActivated: root.imageActivated(imageId)
                    }
                }
            }

            /* ---------------------------- Networks ---------------------------- */
            ColumnLayout {
                id: networksTab

                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Kirigami.Units.smallSpacing

                Kirigami.InlineMessage {
                    objectName: "networksErrorMessage"
                    Layout.fillWidth: true
                    visible: root.controller.networksStateKey === "error"
                    type: Kirigami.MessageType.Error
                    text: i18n("Unable to retrieve the network list: %1", root.controller.networksError)
                }

                /* Search / filter / sort: the same interaction as the container and image lists */
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.TextField {
                        objectName: "networkSearchField"
                        Layout.fillWidth: true
                        placeholderText: i18n("Search by name, ID, driver or subnet…")
                        text: root.networkList.searchText
                        onTextChanged: root.networkList.searchText = text
                    }

                    // Create network (phase 6 §3.3): hidden in read-only mode rather than silently disabled
                    QQC2.Button {
                        objectName: "createNetworkEntryButton"
                        visible: root.operations.writeAllowed
                        text: i18n("Create network…")
                        icon.name: "list-add"
                        onClicked: {
                            createNetworkDialog.reset();
                            createNetworkDialog.open();
                        }
                    }

                    QQC2.ComboBox {
                        id: networkOriginCombo

                        objectName: "networkOriginCombo"
                        textRole: "text"
                        valueRole: "value"
                        model: [
                            {text: i18n("All networks"), value: "all"},
                            {text: i18n("Built-in"), value: "predefined"},
                            {text: i18n("User-defined"), value: "custom"}
                        ]
                        onActivated: root.networkList.originFilter = currentValue
                        Component.onCompleted: currentIndex = indexOfValue(root.networkList.originFilter)
                    }

                    QQC2.ComboBox {
                        id: networkSortCombo

                        objectName: "networkSortCombo"
                        textRole: "text"
                        valueRole: "value"
                        model: [
                            {text: i18n("Name"), value: "name"},
                            {text: i18n("Driver"), value: "driver"},
                            {text: i18n("Scope"), value: "scope"},
                            {text: i18n("Containers"), value: "members"}
                        ]
                        onActivated: root.networkList.sortKey = currentValue
                        Component.onCompleted: currentIndex = indexOfValue(root.networkList.sortKey)
                    }
                }

                Components.EmptyPlaceholder {
                    objectName: "networksEmptyPlaceholder"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    message: root.networkList.count === 0 && root.controller.networkModel.count > 0
                        ? i18n("No network matches the current search or filter.")
                        : root.controller.networkModel.count === 0 ? i18n("No networks found.") : ""
                    explanationText: root.networkList.count === 0 && root.controller.networkModel.count > 0
                        ? i18n("Clear the search field or switch the filter back to “All networks”.")
                        : root.controller.networkModel.count === 0
                            ? i18n("Docker always provides at least the built-in bridge, host and none networks.")
                            : ""
                    actionText: root.networkList.count === 0 && root.controller.networkModel.count > 0 ? i18n("Clear filters") : ""
                    actionIconName: "edit-clear"
                    onActionTriggered: {
                        root.networkList.searchText = "";
                        root.networkList.originFilter = "all";
                        networkOriginCombo.currentIndex = networkOriginCombo.indexOfValue("all");
                        networkSearchField.text = "";
                    }
                }

                ListView {
                    id: networkView

                    objectName: "networkView"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.networkList.count > 0
                    clip: true
                    model: root.networkList
                    spacing: Kirigami.Units.smallSpacing / 2
                    keyNavigationEnabled: true
                    activeFocusOnTab: true

                    QQC2.ScrollBar.vertical: QQC2.ScrollBar {}

                    delegate: NetworkCard {
                        onActivated: root.networkActivated(id)
                    }
                }
            }

            /* ---------------------------- Volumes --------------------------- */
            ColumnLayout {
                id: volumesTab

                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Kirigami.Units.smallSpacing

                Kirigami.InlineMessage {
                    objectName: "volumesErrorMessage"
                    Layout.fillWidth: true
                    visible: root.controller.volumesStateKey === "error"
                    type: Kirigami.MessageType.Error
                    text: i18n("Unable to retrieve the volume list: %1", root.controller.volumesError)
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.TextField {
                        objectName: "volumeSearchField"
                        Layout.fillWidth: true
                        placeholderText: i18n("Search by name, driver or mount point…")
                        text: root.volumeList.searchText
                        onTextChanged: root.volumeList.searchText = text
                    }

                    QQC2.ComboBox {
                        id: volumeUsageCombo

                        objectName: "volumeUsageCombo"
                        textRole: "text"
                        valueRole: "value"
                        model: [
                            {text: i18n("All volumes"), value: "all"},
                            {text: i18n("Unused"), value: "unused"},
                            {text: i18n("In use"), value: "inUse"}
                        ]
                        onActivated: root.volumeList.usageFilter = currentValue
                        Component.onCompleted: currentIndex = indexOfValue(root.volumeList.usageFilter)
                    }

                    QQC2.ComboBox {
                        id: volumeSortCombo

                        objectName: "volumeSortCombo"
                        textRole: "text"
                        valueRole: "value"
                        model: [
                            {text: i18n("Name"), value: "name"},
                            {text: i18n("Driver"), value: "driver"},
                            {text: i18n("Size"), value: "size"},
                            {text: i18n("Containers"), value: "refs"}
                        ]
                        onActivated: root.volumeList.sortKey = currentValue
                        Component.onCompleted: currentIndex = indexOfValue(root.volumeList.sortKey)
                    }

                    // Create volume (hidden in read-only mode)
                    QQC2.Button {
                        objectName: "createVolumeEntryButton"
                        visible: root.operations.writeAllowed
                        text: i18n("Create volume…")
                        icon.name: "list-add"
                        onClicked: root.volumeCreatePanelOpen = !root.volumeCreatePanelOpen
                    }

                    // Prune unused (list the volumes to be deleted first, then confirm)
                    QQC2.Button {
                        objectName: "pruneVolumesEntryButton"
                        visible: root.operations.writeAllowed
                        text: i18n("Clean up unused…")
                        icon.name: "edit-clear"
                        // Read count once to establish the dependency: QML does not track function calls,
                        // so otherwise `enabled` would stay at its initial value after the model fills
                        // (button greyed out forever)
                        enabled: root.controller.volumeModel.count >= 0
                            && root.controller.volumeModel.unusedNames().length > 0
                        onClicked: root.volumePrunePanelOpen = !root.volumePrunePanelOpen
                    }
                }

                /* Create volume: inline panel (as with "create network",
                   popup content is unreliable in offscreen runs) */
                ColumnLayout {
                    objectName: "volumeCreatePanel"
                    Layout.fillWidth: true
                    visible: root.volumeCreatePanelOpen && root.operations.writeAllowed
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.InlineMessage {
                        objectName: "volumeCreateError"
                        Layout.fillWidth: true
                        visible: root.operations.resultKey === "error" && root.operations.resultText.length > 0
                        type: Kirigami.MessageType.Error
                        text: root.operations.resultText
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        QQC2.TextField {
                            id: volumeNameField

                            objectName: "volumeNameField"
                            Layout.fillWidth: true
                            placeholderText: i18n("Volume name")
                            Accessible.name: i18n("Volume name")
                        }

                        QQC2.Button {
                            objectName: "createVolumeButton"
                            text: i18n("Create")
                            icon.name: "list-add"
                            enabled: volumeNameField.text.trim().length > 0
                            onClicked: {
                                if (root.operations.createVolume(volumeNameField.text, "local", [])) {
                                    root.volumeCreatePanelOpen = false;
                                    volumeNameField.text = "";
                                }
                            }
                        }

                        QQC2.Label {
                            Layout.fillWidth: true
                            text: i18n("The volume is created with the local driver.")
                            font: Kirigami.Theme.smallFont
                            opacity: 0.75
                            elide: Text.ElideRight
                        }
                    }
                }

                /* Prune unused: **list the volumes that will be deleted** and the reclaimable space,
                   then ask for confirmation */
                ColumnLayout {
                    objectName: "volumePrunePanel"
                    Layout.fillWidth: true
                    visible: root.volumePrunePanelOpen && root.operations.writeAllowed
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Separator {
                        Layout.fillWidth: true
                    }

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18ncp("@info volumes about to be removed", "One unused volume will be removed:", "%1 unused volumes will be removed:", root.controller.volumeModel.unusedNames().length)
                        font.bold: true
                    }

                    Repeater {
                        model: root.controller.volumeModel.unusedNames()

                        delegate: QQC2.Label {
                            required property string modelData
                            Layout.fillWidth: true
                            text: "• " + modelData
                            font.family: "monospace"
                            elide: Text.ElideMiddle
                        }
                    }

                    QQC2.Label {
                        objectName: "volumePruneSpaceLabel"
                        Layout.fillWidth: true
                        text: i18n("Reclaimable space: %1", root.pruneReclaimableText())
                        opacity: 0.8
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        QQC2.Button {
                            objectName: "pruneVolumesConfirmButton"
                            text: i18n("Clean up")
                            icon.name: "edit-clear"
                            onClicked: {
                                root.operations.pruneVolumes();
                                root.volumePrunePanelOpen = false;
                            }
                        }

                        QQC2.Label {
                            Layout.fillWidth: true
                            text: i18n("Only volumes that no container uses are removed.")
                            font: Kirigami.Theme.smallFont
                            opacity: 0.75
                            elide: Text.ElideRight
                        }
                    }
                }

                Components.EmptyPlaceholder {
                    objectName: "volumesEmptyPlaceholder"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    message: root.volumeList.count === 0 && root.controller.volumeModel.count > 0
                        ? i18n("No volume matches the current search or filter.")
                        : root.controller.volumeModel.count === 0 ? i18n("No volumes found.") : ""
                    // The empty state says only "no data"; what volumes are for belongs in documentation
                    explanationText: root.volumeList.count === 0 && root.controller.volumeModel.count > 0
                        ? i18n("Clear the search field or switch the filter back to “All volumes”.")
                        : ""
                    actionText: root.volumeList.count === 0 && root.controller.volumeModel.count > 0 ? i18n("Clear filters") : ""
                    actionIconName: "edit-clear"
                    onActionTriggered: {
                        root.volumeList.searchText = "";
                        root.volumeList.usageFilter = "all";
                        volumeUsageCombo.currentIndex = volumeUsageCombo.indexOfValue("all");
                        volumeSearchField.text = "";
                    }
                }

                ListView {
                    id: volumeView

                    objectName: "volumeView"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.volumeList.count > 0
                    clip: true
                    model: root.volumeList
                    spacing: Kirigami.Units.smallSpacing / 2
                    keyNavigationEnabled: true
                    activeFocusOnTab: true

                    QQC2.ScrollBar.vertical: QQC2.ScrollBar {}

                    delegate: VolumeCard {
                        onActivated: root.volumeActivated(name)
                    }
                }
            }

            /* ---------------------------- Ports (M3) ---------------------------- */
            /* User requirement: ports are the primary visual focus; a container is just one column */
            ColumnLayout {
                id: portsTab

                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Kirigami.Units.smallSpacing

                Kirigami.InlineMessage {
                    objectName: "portsDeclaredHint"
                    Layout.fillWidth: true
                    // Only explained when such rows really exist (keeps redundant small print down)
                    visible: root.portsDeclaredCount > 0
                    type: Kirigami.MessageType.Warning
                    text: i18ncp("@info", "%1 port is declared by a running container but was not actually published.",
                                 "%1 ports are declared by running containers but were not actually published.",
                                 root.portsDeclaredCount)
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.TextField {
                        objectName: "portSearchField"
                        Layout.fillWidth: true
                        placeholderText: i18n("Search by port, container or image…")
                        text: root.controller.hostPortList.searchText
                        onTextChanged: root.controller.hostPortList.searchText = text
                    }

                    QQC2.ComboBox {
                        objectName: "portStateCombo"
                        textRole: "text"
                        valueRole: "value"
                        model: [
                            {text: i18n("All ports"), value: "all"},
                            {text: i18n("In use"), value: "inUse"},
                            {text: i18n("Not bound"), value: "declaredNotPublished"},
                            // Covers both "not started" and "taken" (name requested by the user)
                            {text: i18n("Not started / taken"), value: "reserved"}
                        ]
                        onActivated: root.controller.hostPortList.stateFilter = currentValue
                        Component.onCompleted: currentIndex = indexOfValue(root.controller.hostPortList.stateFilter)
                    }

                    QQC2.ComboBox {
                        objectName: "portViewCombo"
                        textRole: "text"
                        valueRole: "value"
                        model: [
                            {text: i18n("List"), value: "list"},
                            {text: i18n("Range map"), value: "map"}
                        ]
                        onActivated: root.portViewMode = currentValue
                        Component.onCompleted: currentIndex = indexOfValue(root.portViewMode)
                    }

                    QQC2.ComboBox {
                        objectName: "portSortCombo"
                        // The map lays out by port position, so sorting does not apply: list view only
                        visible: root.portViewMode === "list"
                        textRole: "text"
                        valueRole: "value"
                        model: [
                            {text: i18n("Sort by port"), value: "port"},
                            {text: i18n("Sort by container"), value: "container"}
                        ]
                        onActivated: root.controller.hostPortList.sortKey = currentValue
                        Component.onCompleted: currentIndex = indexOfValue(root.controller.hostPortList.sortKey)
                    }
                }

                Components.EmptyPlaceholder {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    objectName: "portsEmptyPlaceholder"
                    visible: root.portViewMode === "list" && root.controller.hostPortList.count === 0
                    message: root.controller.hostPortList.searchText.length > 0
                        ? i18n("No port matches “%1”.", root.controller.hostPortList.searchText)
                        : i18n("No host port is currently used by a running container.")
                    actionText: root.controller.hostPortList.searchText.length > 0 ? i18n("Clear search") : ""
                    actionIconName: "edit-clear"
                    onActionTriggered: root.controller.hostPortList.searchText = ""
                }

                Components.HostPortRangeMap {
                    objectName: "portRangeMap"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.portViewMode === "map"
                    ranges: root.controller.portRanges
                    nextFreePort: root.controller.nextFreeHostPort
                    onContainerRequested: (containerId, containerName) => root.openContainerFromPorts(containerId, containerName)
                }

                Components.HostPortList {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.portViewMode === "list" && root.controller.hostPortList.count > 0
                    model: root.controller.hostPortList
                    onContainerRequested: (containerId, containerName) => root.openContainerFromPorts(containerId, containerName)
                }
            }

            /* ------------------------ Mount presets (phase 7 §4.2) ------------------------ */
            /* User report: managing presets inside the wizard was awkward, so they became their own tab */
            ColumnLayout {
                id: presetsTab

                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Kirigami.Units.smallSpacing

                Kirigami.Heading {
                    Layout.fillWidth: true
                    level: 3
                    text: i18n("Mount presets")
                }

                Components.MountPresetManager {
                    objectName: "mountPresetManager"
                    Layout.fillWidth: true
                    store: root.controller.mountPresets
                    directoryPicker: root.controller.directoryPicker
                }
            }

            /* ---------------------------- Engine --------------------------- */
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Kirigami.Units.smallSpacing

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.Button {
                        objectName: "openUserRuntimeConfigButton"
                        text: i18n("User configuration…")
                        icon.name: "settings-configure"
                        onClicked: root.configureRuntimeRequested("user")
                    }

                    QQC2.Button {
                        objectName: "openSystemRuntimeConfigButton"
                        text: i18n("System configuration…")
                        icon.name: "settings-configure"
                        onClicked: root.configureRuntimeRequested("system")
                    }

                }

                Components.ServiceCard {
                    Layout.fillWidth: true
                    controller: root.controller.daemonConfigSystem
                    services: root.controller.services
                }

                QQC2.ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    EngineStatusView {
                        width: parent.width
                        engine: root.controller.engine
                        buildStamp: root.controller.buildStamp
                    }
                }
            }
        }
    }

    Components.CreateNetworkDialog {
        id: createNetworkDialog
        operations: root.operations
    }

    Components.PullImageDialog {
        id: pullDialog
        operations: root.operations
        // Credentials already exist for this registry (or the reference is still empty): do not
        // suggest "login needed"; offer "Go to login…" only when there are none
        credentialKnown: pullDialog.referenceInput.serverAddress === ""
            || root.controller.registryAuth.hasCredentialForImage(pullDialog.referenceInput.text)
        onPullRequested: function (reference) {
            root.operations.pullImage(reference);
        }
        onLoginRequested: function (serverAddress) {
            root.registryAuthRequested(serverAddress);
        }
    }
}
