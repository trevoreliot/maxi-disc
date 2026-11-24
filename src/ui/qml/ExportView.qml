// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Screen 2, Export (docs/ui-draft/export.jpeg): connected devices (M8) above the browse
// pane on the left; what goes on the selected device (M9, SyncPane) and its options on the
// right. MiniDisc arrives in M10–M12.
//
// Keyboard: the left column is two panes stacked: j past the last device moves into the
// browse list, k at its top moves back up. l from either moves into the right pane, h back to
// the one it came from. Enter on a device sets up / edits its profile
// (Main opens the dialog), m mounts / unmounts it. In the browse list i (or a double-click)
// adds the row to the device's selection; l moves into the selection, where x / Delete
// removes an entry. p plans a sync, S runs the plan shown (or cancels a running sync), Esc
// closes the plan or stops planning. o switches the right pane to "On Device" (M9b: what's on
// the device; j/k move, Enter folds a header) and back. A NetMD recorder (M10) shows its disc
// instead: j/k move, Enter / n rename the track, N the disc, R reads it again; i adds to its
// "to record" list, o switches disc / to record, x / X remove / clear, S records (or cancels).
Item {
    id: root

    enum Pane { DeviceList, Browse, Selection }

    property bool active: false // this screen has keyboard focus
    // The device pane while there are devices; the browse list when the last one goes. A
    // device plugged in while the user is browsing here doesn't pull the keys away.
    property int focusedPane: ExportView.Browse
    // The left pane the right one was entered from: h goes back there.
    property int leftPane: ExportView.DeviceList
    // With no devices, the device pane has nothing to focus: the browse list has the keys.
    // The right pane (the selection, or the device's music) has the keys only while it lists something.
    // The selected device is a MiniDisc recorder: the right pane shows its disc.
    readonly property bool mdSelected: Devices.currentIndex >= 0 && Devices.current.netMd === true
    readonly property bool rightPaneHasRows: mdSelected ? (MiniDisc.recordView ? MiniDisc.queue.count > 0 : MiniDisc.tracks.count > 0)
                                           : OnDevice.active ? OnDevice.model.count > 0
                                                             : Sync.selection.count > 0 && !syncPane.showsPlan
    readonly property int pane: (focusedPane === ExportView.DeviceList && Devices.count === 0)
                                || (focusedPane === ExportView.Selection && !rightPaneHasRows)
                                ? ExportView.Browse : focusedPane

    readonly property alias browse: browsePane
    readonly property alias deviceList: deviceView
    readonly property alias syncView: syncPane

    Component.onCompleted: if (Devices.count > 0) focusedPane = ExportView.DeviceList
    // An emptied (or replaced by the plan) selection hands the keys back to the browse list.
    Connections {
        target: Sync.selection
        function onCountChanged() {
            if (Sync.selection.count === 0 && !OnDevice.active && root.focusedPane === ExportView.Selection)
                root.focusedPane = ExportView.Browse;
        }
    }
    Connections {
        target: Sync
        function onStateChanged() {
            if (Sync.state !== Sync.Idle && Sync.state !== Sync.Planning && !OnDevice.active
                    && root.focusedPane === ExportView.Selection)
                root.focusedPane = ExportView.Browse;
        }
    }
    Connections {
        target: Devices
        function onCountChanged() {
            if (Devices.count === 0)
                root.focusedPane = ExportView.Browse;
            else if (!root.active)
                root.focusedPane = ExportView.DeviceList; // ready to set up when the user comes here
        }
    }

    signal interacted()
    // Set up / edit a device's profile (Main owns the dialog).
    signal editProfileRequested(string deviceId)
    // Rename the MiniDisc (disc) or its track `index` (Main owns the dialog).
    signal renameMdRequested(bool disc, int index)

    // Ask before something that can't be undone (Main owns the ConfirmDialog; see its ask()).
    signal confirmRequested(var options)

    // M12b: `d` / x / Delete / the button: the marked tracks, else the cursor's.
    function askDeleteMdTracks() {
        if (!MiniDisc.canEdit || MiniDisc.recordView)
            return;
        const rows = MiniDisc.tracks.deleteRows();
        if (rows.length === 0)
            return;
        const line = r => String(r + 1).padStart(2, "0") + "  " + (MiniDisc.tracks.titleAt(r) || qsTr("Untitled"));
        const shown = rows.slice(0, 6).map(line);
        if (rows.length > 6)
            shown.push(qsTr("… and %1 more").arg(rows.length - 6));
        confirmRequested({
            title: rows.length === 1 ? qsTr("Delete track %1?").arg(rows[0] + 1) : qsTr("Delete %1 tracks?").arg(rows.length),
            message: rows.length === 1 ? qsTr("It goes from the MiniDisc for good.") : qsTr("They go from the MiniDisc for good."),
            details: shown,
            confirmText: qsTr("Delete"),
            action: () => MiniDisc.deleteTracks()
        });
    }

    // `D` / the button: the whole disc, after typing WIPE.
    function askEraseMd() {
        if (!MiniDisc.canEdit)
            return;
        const n = MiniDisc.trackCount;
        const disc = MiniDisc.discTitle !== "" ? qsTr("“%1”").arg(MiniDisc.discTitle) : qsTr("the disc");
        confirmRequested({
            title: qsTr("Wipe the disc?"),
            message: (n === 0 ? qsTr("Erases the title of %1.").arg(disc)
                      : n === 1 ? qsTr("Erases the track and the title of %1.").arg(disc)
                      : qsTr("Erases all %1 tracks and the title of %2.").arg(n).arg(disc))
                     + " " + qsTr("It can't be undone."),
            confirmText: qsTr("Wipe disc"),
            phrase: "WIPE",
            action: () => MiniDisc.eraseDisc()
        });
    }

    function renameMdTrack() {
        if (!MiniDisc.hasDisc || MiniDisc.busy)
            return;
        if (MiniDisc.trackCount === 0)
            renameMdRequested(true, -1);
        else
            renameMdRequested(false, Math.max(0, MiniDisc.tracks.currentIndex));
    }

    Connections {
        target: Devices
        // The options box follows the device: MiniDisc or USB / DAP.
        function onCurrentChanged() {
            optionTabs.currentIndex = root.mdSelected ? 0 : 1;
        }
    }

    function editCurrentProfile() {
        if (Devices.currentIndex >= 0 && !mdSelected)
            editProfileRequested(Devices.current.deviceId);
    }

    // i / double-click / the Add button: the browse row goes to the selected device (the
    // MiniDisc's "to record" list or the USB selection); SendToDevice's toast says how many tracks.
    function addCurrentBrowseItem() {
        const item = browsePane.currentItem();
        if (!item) {
            SendToDevice.sendItems([]);
            return;
        }
        if (!mdSelected && SendToDevice.canSend)
            OnDevice.active = false; // show where it went
        SendToDevice.sendItem(item.kind, item.id, item.name);
    }

    // l: into the right pane (if it lists something; else false: on to the queue, if open).
    function enterRightPane(from: int): bool {
        if (!rightPaneHasRows)
            return false;
        leftPane = from;
        focusedPane = ExportView.Selection;
        return true;
    }

    // h: back to the left pane the right one was entered from.
    function leaveRightPane() {
        focusedPane = leftPane === ExportView.DeviceList && Devices.count > 0 ? ExportView.DeviceList : ExportView.Browse;
    }

    function toggleOnDevice() {
        if (pane !== ExportView.Selection)
            leftPane = pane;
        OnDevice.active = !OnDevice.active;
        if (OnDevice.active)
            focusedPane = ExportView.Selection; // the browse list has the keys until rows arrive
    }

    function handleAction(action: int): bool {
        switch (action) {
        case KeyRouter.MountToggle:
            if (Sync.state !== Sync.Planning && Sync.state !== Sync.Syncing) // not under a running sync
                Devices.toggleMountCurrent();
            return true;
        case KeyRouter.ToggleOnDevice:
            if (mdSelected)
                MiniDisc.recordView = !MiniDisc.recordView;
            else
                toggleOnDevice();
            return true;
        case KeyRouter.ClearQueue:
            if (mdSelected && !MiniDisc.recording)
                MiniDisc.clearQueue();
            return true;
        case KeyRouter.CycleRecordMode:
            if (mdSelected)
                MiniDisc.cycleRecordMode();
            return true;
        case KeyRouter.RenameTrack:
            if (mdSelected)
                renameMdTrack();
            return true;
        case KeyRouter.RenameDisc:
            if (mdSelected && MiniDisc.hasDisc && !MiniDisc.busy)
                renameMdRequested(true, -1);
            return true;
        case KeyRouter.RefreshDevice:
            if (mdSelected)
                MiniDisc.refresh();
            else if (OnDevice.active)
                OnDevice.refresh();
            return true;
        case KeyRouter.ExportPlan:
            if (mdSelected) {
                MiniDisc.recordView = true; // the MiniDisc's plan is the "to record" view
                return true;
            }
            if (Sync.canPlan)
                OnDevice.active = false; // the plan shows in the other view
            Sync.plan();
            return true;
        case KeyRouter.ExportSync: // like the button: Cancel while it runs
            if (mdSelected) {
                if (MiniDisc.recording)
                    MiniDisc.cancelRecord();
                else if (MiniDisc.record())
                    MiniDisc.recordView = true;
                return true;
            }
            if (Sync.state === Sync.Planning || Sync.state === Sync.Syncing) {
                Sync.cancel();
            } else if (Sync.canSync) {
                OnDevice.active = false;
                Sync.run();
            }
            return true;
        case KeyRouter.AddToDevice:
            if (pane === ExportView.Browse)
                addCurrentBrowseItem();
            return true;
        case KeyRouter.ExportRemove:
            if (mdSelected) {
                if (pane === ExportView.Selection && MiniDisc.recordView && !MiniDisc.recording)
                    MiniDisc.removeQueued(MiniDisc.queue.currentIndex);
                else if (pane === ExportView.Selection && !MiniDisc.recordView)
                    askDeleteMdTracks(); // the disc's tracks: asks first
                return true;
            }
            if (pane === ExportView.Selection && Sync.state === Sync.Idle && !OnDevice.active)
                syncPane.removeCurrent();
            return true;
        }
        // M12b: editing the disc (the "On disc" list).
        if (mdSelected) {
            switch (action) {
            case KeyRouter.EraseMd:
                askEraseMd();
                return true;
            case KeyRouter.DeleteMdTracks:
                if (pane === ExportView.Selection)
                    askDeleteMdTracks();
                return true;
            case KeyRouter.MoveMdTrackDown:
            case KeyRouter.MoveMdTrackUp:
                if (pane === ExportView.Selection && !MiniDisc.recordView)
                    MiniDisc.moveTrack(action === KeyRouter.MoveMdTrackDown ? 1 : -1);
                return true;
            case KeyRouter.ToggleMark:
                if (pane === ExportView.Selection && !MiniDisc.recordView)
                    MiniDisc.tracks.toggleMark(MiniDisc.tracks.currentIndex);
                return true;
            }
        }
        if (pane === ExportView.Selection && mdSelected) {
            switch (action) {
            case KeyRouter.MoveDown:
                if (MiniDisc.recordView)
                    MiniDisc.queue.move(1);
                else
                    MiniDisc.tracks.move(1);
                return true;
            case KeyRouter.MoveUp:
                if (MiniDisc.recordView)
                    MiniDisc.queue.move(-1);
                else
                    MiniDisc.tracks.move(-1);
                return true;
            case KeyRouter.MoveLeft:
                leaveRightPane();
                return true;
            case KeyRouter.Activate:
                if (!MiniDisc.recordView)
                    renameMdTrack();
                return true;
            }
            return false;
        }
        if (pane === ExportView.Selection && OnDevice.active) {
            switch (action) {
            case KeyRouter.MoveDown:
                OnDevice.model.move(1);
                return true;
            case KeyRouter.MoveUp:
                OnDevice.model.move(-1);
                return true;
            case KeyRouter.MoveLeft:
                leaveRightPane();
                return true;
            case KeyRouter.Activate:
                OnDevice.model.toggle(OnDevice.model.currentIndex);
                return true;
            }
            return false;
        }
        if (pane === ExportView.Selection) {
            switch (action) {
            case KeyRouter.MoveDown:
                syncPane.moveSelection(1);
                return true;
            case KeyRouter.MoveUp:
                syncPane.moveSelection(-1);
                return true;
            case KeyRouter.MoveLeft:
                leaveRightPane();
                return true;
            case KeyRouter.Activate:
                return true;
            }
            return false;
        }
        if (pane === ExportView.DeviceList) {
            switch (action) {
            case KeyRouter.MoveDown:
                if (Devices.currentIndex < Devices.count - 1)
                    Devices.currentIndex = Devices.currentIndex + 1;
                else
                    focusedPane = ExportView.Browse;
                return true;
            case KeyRouter.MoveUp:
                Devices.currentIndex = Math.max(0, Devices.currentIndex - 1);
                return true;
            case KeyRouter.Activate:
                if (mdSelected)
                    enterRightPane(ExportView.DeviceList); // a recorder has no profile: to its tracks
                else
                    editCurrentProfile();
                return true;
            case KeyRouter.MoveLeft:
                return true;
            case KeyRouter.MoveRight:
                return enterRightPane(ExportView.DeviceList);
            }
            return false;
        }
        switch (action) {
        case KeyRouter.MoveDown:
            browsePane.moveSelection(1);
            return true;
        case KeyRouter.MoveUp:
            if (browsePane.atTop && Devices.count > 0)
                focusedPane = ExportView.DeviceList;
            else
                browsePane.moveSelection(-1);
            return true;
        case KeyRouter.Activate:
            browsePane.commit();
            return true;
        case KeyRouter.MoveLeft:
            return true;
        case KeyRouter.MoveRight:
            return enterRightPane(ExportView.Browse);
        }
        return false;
    }

    // Esc: closes the plan or result, stops planning. A running sync is only stopped on
    // purpose (Cancel / S): Esc leaves the screen and the sync goes on.
    function back(): bool {
        if (mdSelected && !MiniDisc.recordView && MiniDisc.tracks.markedCount > 0) {
            MiniDisc.tracks.clearMarks();
            return true;
        }
        if (OnDevice.active)
            return false;
        if (Sync.state === Sync.Ready || Sync.state === Sync.Done) {
            Sync.dismiss();
            return true;
        }
        if (Sync.state === Sync.Planning) {
            Sync.cancel();
            return true;
        }
        return false;
    }

    component ViewTab: Button {
        property bool selected: false
        flat: !selected
        highlighted: selected
        font.pixelSize: Theme.fontHeading
        font.bold: selected
        focusPolicy: Qt.NoFocus
    }
    component SectionTitle: Label {
        font.pixelSize: Theme.fontHeading
        font.bold: true
        color: Theme.textPrimary
    }
    component Badge: Label {
        leftPadding: 6
        rightPadding: 6
        topPadding: 1
        bottomPadding: 1
        font.pixelSize: Theme.fontMicro
        font.letterSpacing: 1
        color: Theme.accent
        background: Rectangle {
            radius: Theme.radiusSmall
            color: "transparent"
            border.color: Theme.accentMuted
        }
    }

    SplitView {
        anchors.fill: parent

        handle: Rectangle {
            implicitWidth: 1
            color: Theme.border
        }

        // Left: devices, then the browse pane.
        ColumnLayout {
            SplitView.preferredWidth: Math.round(root.width * 0.25)
            SplitView.minimumWidth: 190
            SplitView.maximumWidth: 380
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(260, Math.max(130, 52 + Devices.count * 54))
                color: Theme.surface

                FocusBar {
                    active: root.active && root.pane === ExportView.DeviceList
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spaceMD
                    spacing: Theme.spaceSM

                    SectionTitle {
                        text: qsTr("Available Devices")
                    }
                    Label {
                        objectName: "noDevicesLabel"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: Devices.count === 0
                        text: qsTr("No devices detected.\nPlug in a USB drive, a DAP or a NetMD MiniDisc recorder.")
                        wrapMode: Text.Wrap
                        font.pixelSize: Theme.fontSmall
                        color: Theme.textMuted
                    }
                    ListView {
                        id: deviceView
                        objectName: "deviceList"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: Devices.count > 0
                        clip: true
                        model: Devices
                        currentIndex: Devices.currentIndex
                        boundsBehavior: Flickable.StopAtBounds

                        delegate: NavRow {
                            id: deviceRow

                            required property int index
                            required property string deviceId
                            required property string objectPath
                            required property string name
                            required property string spaceText
                            required property bool mounted
                            required property bool busy
                            required property bool hasProfile
                            required property string profileName
                            required property bool netMd

                            width: ListView.view.width
                            leftPadding: Theme.spaceSM
                            rightPadding: Theme.spaceSM
                            current: index === Devices.currentIndex
                            paneActive: root.active && root.pane === ExportView.DeviceList
                            onClicked: {
                                Devices.currentIndex = index;
                                root.focusedPane = ExportView.DeviceList;
                                root.interacted();
                            }
                            onDoubleClicked: root.editProfileRequested(deviceRow.deviceId)

                            contentItem: RowLayout {
                                spacing: Theme.spaceSM
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Label {
                                        Layout.fillWidth: true
                                        text: deviceRow.name
                                        font.bold: true
                                        elide: Text.ElideRight
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: deviceRow.netMd
                                              ? (deviceRow.index === Devices.currentIndex && MiniDisc.statusText !== ""
                                                 ? qsTr("MiniDisc · %1").arg(MiniDisc.statusText) : qsTr("MiniDisc recorder"))
                                              : deviceRow.busy ? qsTr("Working…")
                                              : (deviceRow.hasProfile ? deviceRow.profileName : qsTr("Not set up")) + " · " + deviceRow.spaceText
                                        font.pixelSize: Theme.fontSmall
                                        color: deviceRow.hasProfile || deviceRow.netMd ? Theme.textSecondary : Theme.textMuted
                                        elide: Text.ElideRight
                                    }
                                }
                                Button {
                                    objectName: "mountButton"
                                    visible: !deviceRow.mounted && !deviceRow.netMd
                                    enabled: !deviceRow.busy
                                    text: qsTr("Mount")
                                    flat: true
                                    focusPolicy: Qt.NoFocus
                                    onClicked: Devices.mount(deviceRow.objectPath)
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.border
            }

            BrowsePane {
                id: browsePane
                objectName: "exportBrowse"
                Layout.fillWidth: true
                Layout.fillHeight: true
                showPlaylistTools: false
                paneActive: root.active && root.pane === ExportView.Browse
                onInteracted: {
                    root.focusedPane = ExportView.Browse;
                    root.interacted();
                }
                onRowDoubleClicked: root.addCurrentBrowseItem()
            }
        }

        // Right: what will be exported, and the device's options.
        ColumnLayout {
            SplitView.fillWidth: true
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.margins: Theme.spaceLG
                Layout.bottomMargin: Theme.spaceSM
                spacing: Theme.spaceSM

                SectionTitle {
                    objectName: "mdTitle"
                    visible: root.mdSelected
                    text: qsTr("MiniDisc · %1").arg(MiniDisc.deviceName)
                }
                // The right pane's two views (o switches).
                ViewTab {
                    objectName: "exportTitle"
                    visible: !root.mdSelected
                    selected: !OnDevice.active
                    text: Sync.state === Sync.Syncing ? qsTr("Syncing to %1…").arg(Sync.planSummary.deviceName ?? "")
                        : Sync.state === Sync.Done ? qsTr("Sync finished")
                        : Sync.state === Sync.Ready ? qsTr("Sync plan")
                        : qsTr("Tracks to Export")
                    onClicked: OnDevice.active = false
                }
                ViewTab {
                    objectName: "onDeviceTab"
                    visible: !root.mdSelected
                    selected: OnDevice.active
                    text: qsTr("On Device")
                    onClicked: {
                        if (!OnDevice.active)
                            root.toggleOnDevice();
                        root.interacted();
                    }
                }
                Label {
                    objectName: "exportCount"
                    visible: !root.mdSelected && !OnDevice.active && !syncPane.showsPlan && Sync.tracks.count > 0
                    text: qsTr("%1 tracks · %2").arg(Sync.tracks.count).arg(syncPane.formatDuration(Sync.totalDurationMs))
                    color: Theme.textMuted
                }
                Item {
                    Layout.fillWidth: true
                }
                Button {
                    objectName: "addToExportButton"
                    visible: !root.mdSelected && !OnDevice.active && Sync.state === Sync.Idle && Sync.deviceId !== ""
                    enabled: browsePane.currentList.currentIndex >= 0
                    text: qsTr("Add selected (i)")
                    flat: true
                    focusPolicy: Qt.NoFocus
                    onClicked: root.addCurrentBrowseItem()
                }
                Button {
                    objectName: "closePlanButton"
                    visible: !root.mdSelected && !OnDevice.active && (Sync.state === Sync.Ready || Sync.state === Sync.Done)
                    text: qsTr("Close (Esc)")
                    flat: true
                    focusPolicy: Qt.NoFocus
                    onClicked: Sync.dismiss()
                }
                Button {
                    objectName: "refreshOnDeviceButton"
                    visible: !root.mdSelected && OnDevice.active && Devices.current.mounted === true
                    enabled: !OnDevice.browsing && !OnDevice.paused
                    text: qsTr("Refresh")
                    flat: true
                    focusPolicy: Qt.NoFocus
                    onClicked: OnDevice.refresh()
                }
                Button {
                    objectName: "refreshMdButton"
                    visible: root.mdSelected
                    enabled: !MiniDisc.busy
                    text: qsTr("Refresh (R)")
                    flat: true
                    focusPolicy: Qt.NoFocus
                    onClicked: MiniDisc.refresh()
                }
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: Theme.spaceLG
                Layout.rightMargin: Theme.spaceLG
                currentIndex: root.mdSelected ? 2 : OnDevice.active ? 1 : 0

                SyncPane {
                    id: syncPane
                    objectName: "syncPane"
                    paneActive: root.active && root.pane === ExportView.Selection && !OnDevice.active
                    onInteracted: {
                        root.focusedPane = ExportView.Selection;
                        root.interacted();
                    }
                }
                OnDevicePane {
                    id: onDevicePane
                    objectName: "onDevicePane"
                    paneActive: root.active && root.pane === ExportView.Selection && OnDevice.active && !root.mdSelected
                    onInteracted: {
                        root.focusedPane = ExportView.Selection;
                        root.interacted();
                    }
                }
                MiniDiscPane {
                    id: miniDiscPane
                    objectName: "miniDiscPane"
                    paneActive: root.active && root.pane === ExportView.Selection && root.mdSelected
                    onInteracted: {
                        root.focusedPane = ExportView.Selection;
                        root.interacted();
                    }
                    onRenameTrackRequested: index => root.renameMdRequested(false, index)
                    onRenameDiscRequested: root.renameMdRequested(true, -1)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: false // a child fills height; the row itself stays 180
                Layout.preferredHeight: 220
                Layout.margins: Theme.spaceLG
                spacing: Theme.spaceLG

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignBottom
                    spacing: 2
                    Label {
                        objectName: "selectedDeviceLabel"
                        Layout.fillWidth: true
                        text: Devices.currentIndex >= 0 ? Devices.current.name : qsTr("No device selected")
                        font.bold: Devices.currentIndex >= 0
                        color: Theme.textSecondary
                        elide: Text.ElideRight
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: Devices.currentIndex >= 0
                        text: Devices.current.netMd ? (Devices.current.details ?? "")
                            : Devices.current.mounted ? Devices.current.mountPoint + " · " + Devices.current.spaceText
                            : (Devices.current.details ?? "") + " · " + (Devices.current.spaceText ?? "")
                        font.pixelSize: Theme.fontSmall
                        color: Theme.textMuted
                        elide: Text.ElideMiddle
                    }
                    Label {
                        objectName: "syncErrorLabel"
                        Layout.fillWidth: true
                        visible: Sync.errorString !== ""
                        text: Sync.errorString
                        font.pixelSize: Theme.fontSmall
                        color: Theme.errorText
                        wrapMode: Text.Wrap
                        MouseArea {
                            anchors.fill: parent
                            onClicked: Sync.clearError()
                        }
                    }
                    Label {
                        objectName: "deviceErrorLabel"
                        Layout.fillWidth: true
                        visible: Devices.errorString !== ""
                        text: Devices.errorString
                        font.pixelSize: Theme.fontSmall
                        color: Theme.errorText
                        wrapMode: Text.Wrap
                        MouseArea {
                            anchors.fill: parent
                            onClicked: Devices.clearError()
                        }
                    }
                }

                // Device options (lower right in the drawing): MiniDisc or USB / DAP.
                Rectangle {
                    Layout.preferredWidth: 300
                    Layout.fillHeight: true
                    radius: Theme.radiusMedium
                    color: Theme.surface
                    border.color: Theme.border

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.spaceSM
                        spacing: Theme.spaceSM

                        TabBar {
                            id: optionTabs
                            Layout.fillWidth: true
                            currentIndex: root.mdSelected ? 0 : 1 // follows the device (Connections above)
                            TabButton {
                                text: qsTr("MD options")
                                focusPolicy: Qt.NoFocus
                            }
                            TabButton {
                                text: qsTr("USB / DAP")
                                focusPolicy: Qt.NoFocus
                            }
                        }

                        StackLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            currentIndex: optionTabs.currentIndex

                            GridLayout {
                                columns: 2
                                columnSpacing: Theme.spaceSM
                                rowSpacing: Theme.spaceSM
                                Button {
                                    objectName: "mdDeleteButton"
                                    Layout.fillWidth: true
                                    enabled: root.mdSelected && MiniDisc.canEdit && !MiniDisc.recordView && MiniDisc.trackCount > 0
                                    text: MiniDisc.tracks.markedCount > 0 ? qsTr("Delete %1 (d)").arg(MiniDisc.tracks.markedCount)
                                                                          : qsTr("Delete (d)")
                                    focusPolicy: Qt.NoFocus
                                    onClicked: root.askDeleteMdTracks()
                                    ToolTip.visible: hovered
                                    ToolTip.delay: 600
                                    ToolTip.text: qsTr("The marked tracks (v), else the one under the cursor. J / K move a track.")
                                }
                                Button {
                                    objectName: "mdWipeButton"
                                    Layout.fillWidth: true
                                    enabled: root.mdSelected && MiniDisc.canEdit
                                    text: qsTr("Wipe disc… (D)")
                                    palette.buttonText: Theme.errorText
                                    focusPolicy: Qt.NoFocus
                                    onClicked: root.askEraseMd()
                                }
                                Button {
                                    objectName: "mdRenameButton"
                                    Layout.fillWidth: true
                                    enabled: root.mdSelected && MiniDisc.hasDisc && !MiniDisc.busy && !MiniDisc.recordView
                                    text: qsTr("Rename (n)")
                                    focusPolicy: Qt.NoFocus
                                    onClicked: root.renameMdTrack()
                                }
                                Button {
                                    objectName: "mdRecordButton"
                                    Layout.fillWidth: true
                                    enabled: root.mdSelected && (MiniDisc.recording ? !MiniDisc.cancelling : MiniDisc.plan.canRecord === true)
                                    highlighted: !MiniDisc.recording && MiniDisc.plan.canRecord === true
                                    text: MiniDisc.cancelling ? qsTr("Stopping…") : MiniDisc.recording ? qsTr("Cancel (S)") : qsTr("Record (S)")
                                    focusPolicy: Qt.NoFocus
                                    onClicked: {
                                        if (MiniDisc.recording) {
                                            MiniDisc.cancelRecord();
                                        } else if (MiniDisc.record()) {
                                            MiniDisc.recordView = true;
                                        }
                                    }
                                }
                                // M12: how the next recording goes on the disc (M cycles).
                                RowLayout {
                                    objectName: "mdModeBar"
                                    Layout.columnSpan: 2
                                    spacing: 0
                                    enabled: root.mdSelected && !MiniDisc.recording

                                    Repeater {
                                        model: [qsTr("SP"), qsTr("LP2"), qsTr("LP4")]
                                        delegate: Button {
                                            id: modeButton
                                            required property int index
                                            required property string modelData
                                            objectName: "mdMode" + modelData
                                            Layout.fillWidth: true
                                            text: modelData
                                            checkable: true
                                            checked: MiniDisc.recordMode === index
                                            highlighted: checked
                                            enabled: index === 0 || MiniDisc.lpAvailable
                                            focusPolicy: Qt.NoFocus
                                            onClicked: MiniDisc.recordMode = index
                                            ToolTip.visible: hovered
                                            ToolTip.delay: 600
                                            ToolTip.text: index === 0 ? qsTr("SP: best quality, 80 minutes on an 80-minute disc (M cycles)")
                                                        : index === 1 ? qsTr("LP2: 132 kbps ATRAC3, twice the music (M cycles)")
                                                        : qsTr("LP4: 66 kbps ATRAC3, four times the music (M cycles)")
                                        }
                                    }
                                }
                                Label {
                                    objectName: "mdModeNote"
                                    Layout.columnSpan: 2
                                    Layout.fillWidth: true
                                    text: !MiniDisc.lpAvailable ? MiniDisc.lpUnavailableReason
                                        : MiniDisc.recordMode === 0 ? qsTr("SP · the deck encodes; 1× time")
                                        : MiniDisc.recordMode === 1 ? qsTr("LP2 · 132 kbps; 2× time")
                                        : qsTr("LP4 · 66 kbps; 4× time")
                                    font.pixelSize: Theme.fontMicro
                                    color: MiniDisc.lpAvailable ? Theme.textMuted : Theme.errorText
                                    wrapMode: Text.Wrap
                                }
                            }
                            ColumnLayout {
                                spacing: Theme.spaceXS

                                Label {
                                    objectName: "usbOptionsHint"
                                    Layout.fillWidth: true
                                    visible: Devices.currentIndex < 0
                                    text: qsTr("Select a USB drive or DAP to set it up.")
                                    font.pixelSize: Theme.fontSmall
                                    color: Theme.textMuted
                                    wrapMode: Text.Wrap
                                }
                                Label {
                                    objectName: "profileSummary"
                                    Layout.fillWidth: true
                                    visible: Devices.currentIndex >= 0
                                    text: Devices.currentProfile.name !== undefined
                                          ? qsTr("<b>%1</b> · %2<br>%3 · art: %4")
                                                .arg(Devices.currentProfile.name).arg(Devices.currentProfile.formats)
                                                .arg(Devices.currentProfile.transcode).arg(Devices.currentProfile.artwork)
                                          : qsTr("<b>%1</b> isn't set up yet.").arg(Devices.current.name ?? "")
                                    textFormat: Text.StyledText
                                    font.pixelSize: Theme.fontSmall
                                    wrapMode: Text.Wrap
                                    maximumLineCount: 3
                                    elide: Text.ElideRight
                                }
                                Label {
                                    Layout.fillWidth: true
                                    visible: Devices.currentProfile.example !== undefined
                                    text: Devices.currentProfile.example ?? ""
                                    font.family: Theme.fontMono
                                    font.pixelSize: Theme.fontMicro
                                    color: Theme.textMuted
                                    elide: Text.ElideMiddle
                                }
                                Item { Layout.fillHeight: true }
                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: 2
                                    columnSpacing: Theme.spaceSM
                                    rowSpacing: Theme.spaceSM
                                    Button {
                                        objectName: "editProfileButton"
                                        Layout.fillWidth: true
                                        enabled: Devices.currentIndex >= 0
                                        text: Devices.current.hasProfile ? qsTr("Edit profile…") : qsTr("Set up…")
                                        highlighted: Devices.currentIndex >= 0 && !Devices.current.hasProfile
                                        focusPolicy: Qt.NoFocus
                                        onClicked: root.editCurrentProfile()
                                    }
                                    Button {
                                        objectName: "mountToggleButton"
                                        Layout.fillWidth: true
                                        enabled: Devices.currentIndex >= 0 && !Devices.current.busy
                                                 && Sync.state !== Sync.Syncing && Sync.state !== Sync.Planning
                                        text: Devices.current.mounted ? qsTr("Unmount") : qsTr("Mount")
                                        focusPolicy: Qt.NoFocus
                                        onClicked: Devices.toggleMountCurrent()
                                    }
                                    Button {
                                        objectName: "planSyncButton"
                                        Layout.fillWidth: true
                                        enabled: Sync.canPlan
                                        text: qsTr("Plan sync (p)")
                                        focusPolicy: Qt.NoFocus
                                        onClicked: Sync.plan()
                                    }
                                    Button {
                                        objectName: "syncButton"
                                        Layout.fillWidth: true
                                        readonly property bool stopping: Sync.state === Sync.Syncing || Sync.state === Sync.Planning
                                        enabled: stopping || Sync.canSync
                                        highlighted: Sync.canSync
                                        text: stopping ? qsTr("Cancel") : qsTr("Sync (S)")
                                        focusPolicy: Qt.NoFocus
                                        onClicked: stopping ? Sync.cancel() : Sync.run()
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
