// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Screen 2, Export (docs/ui-draft/export.jpeg): connected devices (M8) above the browse
// pane on the left; what to export (M9) and the selected device's options on the right.
// USB / DAP sync arrives in M9, MiniDisc in M10–M12.
//
// Keyboard: the left column is two panes stacked: j past the last device moves into the
// browse list, k at its top moves back up. Enter on a device sets up / edits its profile
// (Main opens the dialog), m mounts / unmounts it.
Item {
    id: root

    enum Pane { DeviceList, Browse }

    property bool active: false // this screen has keyboard focus
    // The device pane while there are devices; the browse list when the last one goes. A
    // device plugged in while the user is browsing here doesn't pull the keys away.
    property int focusedPane: ExportView.Browse
    // With no devices, the device pane has nothing to focus: the browse list has the keys.
    readonly property int pane: focusedPane === ExportView.DeviceList && Devices.count === 0 ? ExportView.Browse
                                                                                           : focusedPane

    readonly property alias browse: browsePane
    readonly property alias deviceList: deviceView

    Component.onCompleted: if (Devices.count > 0) focusedPane = ExportView.DeviceList
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

    function editCurrentProfile() {
        if (Devices.currentIndex >= 0)
            editProfileRequested(Devices.current.deviceId);
    }

    function handleAction(action: int): bool {
        if (action === KeyRouter.MountToggle) {
            Devices.toggleMountCurrent();
            return true;
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
                editCurrentProfile();
                return true;
            case KeyRouter.MoveLeft:
                return true;
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
        }
        return false; // MoveRight: nothing to the right yet (the queue, if open)
    }

    function back(): bool {
        return false;
    }

    // A device action that isn't built yet: visibly inert.
    component StubButton: Button {
        enabled: false
        opacity: 0.45
        focusPolicy: Qt.NoFocus
        Layout.fillWidth: true
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
                        text: qsTr("No devices detected.\nPlug in a USB drive or a DAP (NetMD recorders: M10).")
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
                                        text: deviceRow.busy ? qsTr("Working…")
                                            : (deviceRow.hasProfile ? deviceRow.profileName : qsTr("Not set up")) + " · " + deviceRow.spaceText
                                        font.pixelSize: Theme.fontSmall
                                        color: deviceRow.hasProfile ? Theme.textSecondary : Theme.textMuted
                                        elide: Text.ElideRight
                                    }
                                }
                                Button {
                                    objectName: "mountButton"
                                    visible: !deviceRow.mounted
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
                paneActive: root.active
                onInteracted: root.interacted()
            }
        }

        // Right: what will be exported, and the device's options.
        ColumnLayout {
            SplitView.fillWidth: true
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.margins: Theme.spaceLG
                spacing: Theme.spaceSM

                SectionTitle {
                    text: qsTr("Tracks to Export")
                }
                Badge {
                    text: qsTr("PREVIEW")
                }
                Item {
                    Layout.fillWidth: true
                }
            }

            Label {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: Theme.spaceLG
                Layout.rightMargin: Theme.spaceLG
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                wrapMode: Text.Wrap
                text: qsTr("Choosing music to copy to a USB drive or DAP, or to record to a MiniDisc, arrives with device support.")
                color: Theme.textMuted
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
                        text: Devices.current.mounted ? Devices.current.mountPoint + " · " + Devices.current.spaceText
                                                      : (Devices.current.details ?? "") + " · " + (Devices.current.spaceText ?? "")
                        font.pixelSize: Theme.fontSmall
                        color: Theme.textMuted
                        elide: Text.ElideMiddle
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
                            currentIndex: 1 // USB / DAP: the only devices so far
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
                                StubButton { text: qsTr("Wipe disc") }
                                StubButton { text: qsTr("Arrange tracks") }
                                StubButton { text: qsTr("Rename") }
                                StubButton { text: qsTr("Sync") }
                                Label {
                                    Layout.columnSpan: 2
                                    text: qsTr("NetMD support: M10–M12")
                                    font.pixelSize: Theme.fontMicro
                                    color: Theme.textMuted
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
                                        text: Devices.current.mounted ? qsTr("Unmount") : qsTr("Mount")
                                        focusPolicy: Qt.NoFocus
                                        onClicked: Devices.toggleMountCurrent()
                                    }
                                    StubButton { text: qsTr("Plan sync") }
                                    StubButton { text: qsTr("Sync") }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
